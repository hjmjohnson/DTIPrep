/*=========================================================================

Module:    $RCSfile: DicomToNrrdConverter.cxx,v $
Language:  C++
Date:      $Date: 2009-11-25 18:02:21 $
Version:   $Revision: 1.7 $

This work is part of the National Alliance for Medical Image
Computing (NAMIC), funded by the National Institutes of Health
through the NIH Roadmap for Medical Research, Grant U54 EB005149.

See License.txt or http://www.slicer.org/copyright/copyright.txt for details.

 ***
 This program converts Diffusion weighted MR images in Dicom format into
 NRRD format.

Assumptions:

1) Uses left-posterior-superior (Dicom default) as default space for philips and siemens.  
This is the default space for NRRD header.
2) For GE data, Dicom data are arranged in volume interleaving order.
3) For Siemens data, images are arranged in mosaic form.
4) For oblique collected Philips data, the measurement frame for the
gradient directions is the same as the ImageOrientationPatient

=========================================================================*/
#if defined(_MSC_VER)
#pragma warning ( disable : 4786 )
#endif

#ifdef __BORLANDC__
#define ITK_LEAN_AND_MEAN
#endif

#include <iostream>
#include <fstream>
#include <string>
#include <iomanip>

#include "itkXMLFilterWatcher.h"

#include "itkNrrdImageIO.h"
#include "itkOrientedImage.h"
#include "itkImageSeriesReader.h"
#include "itkMetaDataDictionary.h"
#include "itkSmoothingRecursiveGaussianImageFilter.h"
#include "itkRecursiveGaussianImageFilter.h"


#include "itkImageFileWriter.h"
#include "itkRawImageIO.h"
#include "itkGDCMImageIO.h"
#include "itkGDCMSeriesFileNames.h"
#include "itkNumericSeriesFileNames.h"

//#include "itkScalarImageToListAdaptor.h"
//#include "itkListSampleToHistogramGenerator.h"
//
#include "itksys/Base64.h"

#include "itkImage.h"
#include "vnl/vnl_math.h"

#include "itkImageFileReader.h"
#include "itkImageFileWriter.h"
#include "itkImageRegionIteratorWithIndex.h"

#include "vnl/vnl_vector_fixed.h"
#include "vnl/vnl_matrix_fixed.h"
#include "vnl/algo/vnl_svd.h"

#include "itkVectorImage.h"
#include "gdcmFile.h"
#include "gdcmGlobal.h"
#include "gdcmUtil.h"
#include "gdcmDebug.h"
#include "gdcmTS.h"
#include "gdcmValEntry.h"
#include "gdcmBinEntry.h"
#include "gdcmSeqEntry.h"
#include "gdcmRLEFramesInfo.h"
#include "gdcmJPEGFragmentsInfo.h"
#include "gdcmSQItem.h"

#include "gdcmSeqEntry.h"
#include "gdcmDictSet.h"        // access to dictionary
#include "gdcmDict.h"           // access to dictionary
#include "gdcmFile.h"           // access to dictionary
#include "gdcmDocEntry.h"       //internal of gdcm
#include "gdcmBinEntry.h"       //internal of gdcm
#include "gdcmValEntry.h"       //internal of gdcm
#include "gdcmDictEntry.h"      // access to dictionary
#include "gdcmGlobal.h"         // access to dictionary

#include "itksys/RegularExpression.hxx"
#include "itksys/Directory.hxx"
#include "itksys/SystemTools.hxx"

#include "DicomToNrrdConverterCLP.h"

// Use an anonymous namespace to keep class types and function names
// from colliding when module is used as shared object module.  Every
// thing should be in an anonymous namespace except for the module
// entry point, e.g. main()
//
namespace {

  // relevant GE private tags
  const gdcm::DictEntry GEDictBValue( 0x0043, 0x1039, "IS", "1", "B Value of diffusion weighting" );
  const gdcm::DictEntry GEDictXGradient( 0x0019, 0x10bb, "DS", "1", "X component of gradient direction" );
  const gdcm::DictEntry GEDictYGradient( 0x0019, 0x10bc, "DS", "1", "Y component of gradient direction" );
  const gdcm::DictEntry GEDictZGradient( 0x0019, 0x10bd, "DS", "1", "Z component of gradient direction" );

  // relevant Siemens private tags
  const gdcm::DictEntry SiemensMosiacParameters( 0x0051, 0x100b, "IS", "1", "Mosiac Matrix Size" );
  const gdcm::DictEntry SiemensDictNMosiac( 0x0019, 0x100a, "US", "1", "Number of Images In Mosaic" );
  const gdcm::DictEntry SiemensDictBValue( 0x0019, 0x100c, "IS", "1", "B Value of diffusion weighting" );
  const gdcm::DictEntry SiemensDictDiffusionDirection( 0x0019, 0x100e, "FD", "3", "Diffusion Gradient Direction" );
  const gdcm::DictEntry SiemensDictDiffusionMatrix( 0x0019, 0x1027, "FD", "6", "Diffusion Matrix" );
  const gdcm::DictEntry SiemensDictShadowInfo( 0x0029, 0x1010, "OB", "1", "Siemens DWI Info" );

  // relevant Philips private tags
  const gdcm::DictEntry PhilipsDictBValue              ( 0x2001, 0x1003, "FL", "1", "B Value of diffusion weighting" );
  const gdcm::DictEntry PhilipsDictDiffusionDirection  ( 0x2001, 0x1004, "CS", "1", "Diffusion Gradient Direction" );
  const gdcm::DictEntry PhilipsDictDiffusionDirectionRL( 0x2005, 0x10b0, "FL", "4", "Diffusion Direction R/L" );
  const gdcm::DictEntry PhilipsDictDiffusionDirectionAP( 0x2005, 0x10b1, "FL", "4", "Diffusion Direction A/P" );
  const gdcm::DictEntry PhilipsDictDiffusionDirectionFH( 0x2005, 0x10b2, "FL", "4", "Diffusion Direction F/H" );

#if 1 //Defined in gdcm dicmoV3.dic
  // Tags defined in Supplement 49 
  // 0018 9075 CS 1 Diffusion Directionality
  // 0018 9076 SQ 1 Diffusion Gradient Direction Sequence
  // 0018 9087 FD 1 Diffusion b-value
  // 0018 9089 FD 3 Diffusion Gradient Orientation
  // 0018 9117 SQ 1 MR Diffusion Sequence
  // 0018 9147 CS 1 Diffusion Anisotropy Type
  const gdcm::DictEntry Supplement49DictDiffusionDirection                ( 0x0018, 0x9075, "CS", "1", "Diffusion Directionality");
  const gdcm::DictEntry Supplement49DictDiffusionGradientDirectionSequence( 0x0018, 0x9076, "SQ", "1", "Diffusion Gradient Direction Sequence" );
  const gdcm::DictEntry Supplement49DictDiffusionBValue                   ( 0x0018, 0x9087, "FD", "1", "Diffusion b-value" );
  const gdcm::DictEntry Supplement49DictDiffusionGradientOrientation      ( 0x0018, 0x9089, "FD", "3", "Diffusion Graident Orientation" );
  const gdcm::DictEntry Supplement49DictMRDiffusionSequence               ( 0x0018, 0x9117, "SQ", "1", "MR Diffusion Sequence" );
  const gdcm::DictEntry Supplement49DictDiffusionAnisotropyType           ( 0x0018, 0x9147, "CS", "1", "Diffusion Anisotropy Type" );
#endif

  // add relevant private tags from other vendors

  typedef vnl_vector_fixed<double, 3> VectorType;
  typedef itk::Vector<float, 3> OutputVectorType;

  typedef short PixelValueType;
  typedef itk::OrientedImage< PixelValueType, 3 > VolumeType;
  typedef itk::ImageSeriesReader< VolumeType > ReaderType;
  typedef itk::GDCMImageIO ImageIOType;
  typedef itk::GDCMSeriesFileNames InputNamesGeneratorType;


  bool ExtractBinValEntry( gdcm::File * header, uint16_t group, uint16_t elem, std::string& tag )
  {
    tag.clear();
    if ( header->GetBinEntry(group, elem) )
    {
      gdcm::BinEntry* binEntry = header->GetBinEntry(group, elem);
      int binLength = binEntry->GetFullLength();
      tag.resize( binLength );
      uint8_t * tagString = binEntry->GetBinArea();

      for (int n = 0; n < binLength; n++)
      {
        tag[n] = *(tagString+n);
      }
      return true;
    }
    else if ( header->GetValEntry(group, elem) )
    {
      tag = header->GetValEntry(group, elem)->GetValue();
      return true;
    }
    return false;
  }

  int ExtractSiemensDiffusionInformation( std::string tagString, std::string nameString, std::vector<double>& valueArray )
    {
    ::size_t atPosition = tagString.find( nameString );
    if ( atPosition == std::string::npos)
      {
      return 0;
      }
    else
      {
      std::string infoAsString = tagString.substr( atPosition, tagString.size()-atPosition+1 );
      const char * infoAsCharPtr = infoAsString.c_str();

      int vm = *(infoAsCharPtr+64);
        {
        std::string vr = infoAsString.substr( 68, 2 );
        int syngodt = *(infoAsCharPtr+72);
        int nItems = *(infoAsCharPtr+76);
        int localDummy = *(infoAsCharPtr+80);

        //std::cout << "\tName String: " << nameString << std::endl;
        //std::cout << "\tVR: " << vr << std::endl;
        //std::cout << "\tVM: " << vm << std::endl;
        //std::cout << "Local String: " << infoAsString.substr(0,80) << std::endl;

        /* This hack is required for some Siemens VB15 Data */
        if ( ( nameString == "DiffusionGradientDirection" ) && (vr != "FD") )
          {
          bool loop = true;
          while ( loop )
            {
            atPosition = tagString.find( nameString, atPosition+26 );
            if ( atPosition == std::string::npos)
              {
              //std::cout << "\tFailed to find DiffusionGradientDirection Tag - returning" << vm << std::endl;
              return 0;
              }
            infoAsString = tagString.substr( atPosition, tagString.size()-atPosition+1 );
            infoAsCharPtr = infoAsString.c_str();
            //std::cout << "\tOffset to new position" << std::endl;
            //std::cout << "\tNew Local String: " << infoAsString.substr(0,80) << std::endl;
            vm = *(infoAsCharPtr+64);
            vr = infoAsString.substr( 68, 2 );
            if (vr == "FD") loop = false;
            syngodt = *(infoAsCharPtr+72);
            nItems = *(infoAsCharPtr+76);
            localDummy = *(infoAsCharPtr+80);
            //std::cout << "\tVR: " << vr << std::endl;
            //std::cout << "\tVM: " << vm << std::endl;
            }
          }
        else
          {
          //std::cout << "\tUsing initial position" << std::endl;
          }
        //std::cout << "\tArray Length: " << vm << std::endl;
        }

      int offset = 84;
      for (int k = 0; k < vm; k++)
        {
        int itemLength = *(infoAsCharPtr+offset+4);
        int strideSize = static_cast<int> (ceil(static_cast<double>(itemLength)/4) * 4);
        std::string valueString = infoAsString.substr( offset+16, itemLength );
        valueArray.push_back( atof(valueString.c_str()) );
        offset += 16+strideSize;
        }
      return vm;
      }
    }

} // end of anonymous namespace


int main(int argc, char* argv[])
{
  PARSE_ARGS;

#if 1 //Defined in gdcm dicomV3.dic
  gdcm::Global::GetDicts()->GetDefaultPubDict()->AddEntry(Supplement49DictDiffusionDirection);
  gdcm::Global::GetDicts()->GetDefaultPubDict()->AddEntry(Supplement49DictDiffusionGradientDirectionSequence);
  gdcm::Global::GetDicts()->GetDefaultPubDict()->AddEntry(Supplement49DictDiffusionBValue);
  gdcm::Global::GetDicts()->GetDefaultPubDict()->AddEntry(Supplement49DictDiffusionGradientOrientation);

  gdcm::Global::GetDicts()->GetDefaultPubDict()->AddEntry(Supplement49DictMRDiffusionSequence);
  gdcm::Global::GetDicts()->GetDefaultPubDict()->AddEntry(Supplement49DictDiffusionAnisotropyType);

  gdcm::DictEntry * dictEntry=gdcm::Global::GetDicts()->GetDefaultPubDict()->GetEntry(0x0018,0x9089);
  dictEntry->Print();

#endif

  bool SliceOrderIS = true;
  bool SliceMosaic = false;
  bool SingleSeries = true;

  // check if the file name is valid
  std::string nhdrname = outputVolume;
  std::string dataname;
    {
    ::size_t i = nhdrname.find(".nhdr");
    if (i == std::string::npos)
      {
      std::cerr << "Output file must be a nrrd header file.\n";
      std::cerr << "Version:   $Revision: 1.7 $" << std::endl;
      return EXIT_FAILURE;
      }
    dataname = nhdrname.substr(0, i) + ".raw";
    }

  //////////////////////////////////////////////////
  // 0a) read one slice and figure out vendor
  InputNamesGeneratorType::Pointer inputNames = InputNamesGeneratorType::New();
  inputNames->SetInputDirectory( inputDicomDirectory.c_str() );

  ReaderType::FileNamesContainer filenames;

  const ReaderType::FileNamesContainer & filenamesInSeries =
    inputNames->GetInputFileNames();

  //HACK:  This is not true.  The Philips scanner has the ability to write a multi-frame single file for DTI data.
  //
  // If there are just one file in the serires returned above, it is obvious that the series is
  // not complete. There is no way to put diffusion weighted images in one file, even for mosaic
  // format. We, then, need to find all files in that directory and treate them as a single series
  // of diffusion weighted image.
  if ( filenamesInSeries.size() > 1 )
    {
    ::size_t nFiles = filenamesInSeries.size();
    filenames.resize( 0 );
    for (::size_t k = 0; k < nFiles; k++)
      {
      filenames.push_back( filenamesInSeries[k] );
      }
    }
  else
    {
    std::cout << "gdcm returned just one file. \n";
    SingleSeries = false;
    filenames.resize( 0 );
    itksys::Directory directory;
    directory.Load( itksys::SystemTools::CollapseFullPath(inputDicomDirectory.c_str()).c_str() );
    ImageIOType::Pointer gdcmIOTest = ImageIOType::New();

    // for each patient directory
    for ( unsigned int k = 0; k < directory.GetNumberOfFiles(); k++)
      {
      std::string subdirectory( inputDicomDirectory.c_str() );
      subdirectory = subdirectory + "/" + directory.GetFile(k);

      std::string sqDir( directory.GetFile(k) );
      if (sqDir.length() == 1 && directory.GetFile(k)[0] == '.')   // skip self
        {
        continue;
        }
      else if (sqDir.length() == 2 && sqDir.find( ".." ) != std::string::npos)    // skip parent
        {
        continue;
        }
      else if (!itksys::SystemTools::FileIsDirectory( subdirectory.c_str() ))     // load only files
        {
        if ( gdcmIOTest->CanReadFile(subdirectory.c_str()) )
          {
          filenames.push_back( subdirectory );
          }
        }
      }
    for (::size_t k = 0; k < filenames.size(); k++)
      {
      std::cout << "itksys file names: " << filenames[k] << std::endl;
      }
    }

  gdcm::File * headerLite = new gdcm::File;
  headerLite->SetFileName( filenames[0] );
  headerLite->SetMaxSizeLoadEntry( 65535 );
  headerLite->SetLoadMode( gdcm::LD_NOSEQ );
  headerLite->Load();

  // 
  std::vector<int> ignorePhilipsSliceMultiFrame;

  // check the tag 0008|0070 for vendor information
  std::string vendor;
  ExtractBinValEntry( headerLite, 0x0008, 0x0070, vendor );
  for (unsigned int k = 0; k < vendor.size(); k++)
  {
    vendor[k] =  toupper( vendor[k] );
  }

  std::string ImageType;
  ExtractBinValEntry( headerLite, 0x0008, 0x0008, ImageType );
  std::cout << ImageType << std::endl;

  //////////////////////////////////////////////////////////////////
  //  0b) Add private dictionary
  if ( vendor.find("GE") != std::string::npos )
    {
    // for GE data
    gdcm::Global::GetDicts()->GetDefaultPubDict()->AddEntry(GEDictBValue);
    gdcm::Global::GetDicts()->GetDefaultPubDict()->AddEntry(GEDictXGradient);
    gdcm::Global::GetDicts()->GetDefaultPubDict()->AddEntry(GEDictYGradient);
    gdcm::Global::GetDicts()->GetDefaultPubDict()->AddEntry(GEDictZGradient);
    std::cout << "Image acquired using a GE scanner\n";
    }
  else if( vendor.find("SIEMENS") != std::string::npos )
    {
    // for Siemens data
    gdcm::Global::GetDicts()->GetDefaultPubDict()->AddEntry(SiemensMosiacParameters);
    gdcm::Global::GetDicts()->GetDefaultPubDict()->AddEntry(SiemensDictNMosiac);
    gdcm::Global::GetDicts()->GetDefaultPubDict()->AddEntry(SiemensDictBValue);
    gdcm::Global::GetDicts()->GetDefaultPubDict()->AddEntry(SiemensDictDiffusionDirection);
    gdcm::Global::GetDicts()->GetDefaultPubDict()->AddEntry(SiemensDictDiffusionMatrix);
    gdcm::Global::GetDicts()->GetDefaultPubDict()->AddEntry(SiemensDictShadowInfo);
    std::cout << "Image acquired using a Siemens scanner\n";

    if ( ImageType.find("MOSAIC") != std::string::npos )
      {
      std::cout << "Siemens Mosaic format\n";
      SliceMosaic = true;
      }
    else
      {
      std::cout << "Siemens split format\n";
      SliceMosaic = false;
      }
    }
  else if ( ( vendor.find("PHILIPS") != std::string::npos ) )
    {
    // for philips data
    std::cout << "Image acquired using a Philips scanner\n";
    gdcm::Global::GetDicts()->GetDefaultPubDict()->AddEntry(PhilipsDictBValue);
    gdcm::Global::GetDicts()->GetDefaultPubDict()->AddEntry(PhilipsDictDiffusionDirection);
    gdcm::Global::GetDicts()->GetDefaultPubDict()->AddEntry(PhilipsDictDiffusionDirectionRL);
    gdcm::Global::GetDicts()->GetDefaultPubDict()->AddEntry(PhilipsDictDiffusionDirectionAP);
    gdcm::Global::GetDicts()->GetDefaultPubDict()->AddEntry(PhilipsDictDiffusionDirectionFH);
    }
  else
    {
    std::cerr << "Unrecognized vendor.\n" << std::endl;
    }


  //////////////////////////////////////////////////
  // 1) Read the input series as an array of slices
  ReaderType::Pointer reader = ReaderType::New();
  itk::GDCMImageIO::Pointer gdcmIO = itk::GDCMImageIO::New();
  reader->SetImageIO( gdcmIO );
  reader->SetFileNames( filenames );
  const unsigned int nSlice = filenames.size();
  try
    {
    reader->Update();
    }
  catch (itk::ExceptionObject &excp)
    {
    std::cerr << "Exception thrown while reading the series" << std::endl;
    std::cerr << excp << std::endl;
    return EXIT_FAILURE;
    }

  //////////////////////////////////////////////////
  // 1-A) Read the input dicom headers
  std::vector<gdcm::File *> allHeaders( filenames.size() );
  for (unsigned int k = 0; k < filenames.size(); k ++)
  {
    allHeaders[k] = new gdcm::File;
    allHeaders[k]->SetFileName( filenames[k] );
    allHeaders[k]->SetMaxSizeLoadEntry( 65535 );
    allHeaders[k]->SetLoadMode( gdcm::LD_NOSEQ );
    allHeaders[k]->Load();
  }

  /////////////////////////////////////////////////////
  // 2) Analyze the DICOM header to determine the
  //    number of gradient directions, gradient
  //    vectors, and form volume based on this info


  // load in all public tags
  std::string tag;

  // number of rows is the number of pixels in the second dimension
  ExtractBinValEntry( allHeaders[0], 0x0028, 0x0010, tag );
  int nRows = atoi( tag.c_str() );

  // number of columns is the number of pixels in the first dimension
  ExtractBinValEntry( allHeaders[0], 0x0028, 0x0011, tag );
  int nCols = atoi( tag.c_str() );

  ExtractBinValEntry( allHeaders[0], 0x0028, 0x0030, tag );
  float xRes;
  float yRes;
  sscanf( tag.c_str(), "%f\\%f", &xRes, &yRes );

  ExtractBinValEntry( allHeaders[0], 0x0020, 0x0032, tag );
  itk::Vector<double,3> ImageOrigin;
  sscanf( tag.c_str(), "%lf\\%lf\\%lf", &(ImageOrigin[0]), &(ImageOrigin[1]), &(ImageOrigin[2]) );

  ExtractBinValEntry( allHeaders[0], 0x0018, 0x0088, tag );
  float sliceSpacing = atof( tag.c_str() );

  //Make a hash of the sliceLocations in order to get the correct count.  This is more reliable since SliceLocation may not be available.
  std::map<std::string,int> sliceLocations;
  for (unsigned int k = 0; k < nSlice; k++)
    {
      ExtractBinValEntry( allHeaders[k], 0x0020, 0x0032, tag );
      sliceLocations[tag]++;
    }
  unsigned int numberOfSlicesPerVolume=sliceLocations.size();
  std::cout << "=================== numberOfSlicesPerVolume:" << numberOfSlicesPerVolume << std::endl;

  itk::Matrix<double,3,3> MeasurementFrame;
  MeasurementFrame.SetIdentity();

  // check ImageOrientationPatient and figure out slice direction in
  // L-P-I (right-handed) system.
  // In Dicom, the coordinate frame is L-P by default. Look at
  // http://medical.nema.org/dicom/2007/07_03pu.pdf ,  page 301
  ExtractBinValEntry( allHeaders[0], 0x0020, 0x0037, tag );
  itk::Matrix<double,3,3> LPSDirCos;
  sscanf( tag.c_str(), "%lf\\%lf\\%lf\\%lf\\%lf\\%lf",
    &(LPSDirCos[0][0]), &(LPSDirCos[1][0]), &(LPSDirCos[2][0]),
    &(LPSDirCos[0][1]), &(LPSDirCos[1][1]), &(LPSDirCos[2][1]) );
  // Cross product, this gives I-axis direction
  LPSDirCos[0][2] = (LPSDirCos[1][0]*LPSDirCos[2][1]-LPSDirCos[2][0]*LPSDirCos[1][1]);
  LPSDirCos[1][2] = (LPSDirCos[2][0]*LPSDirCos[0][1]-LPSDirCos[0][0]*LPSDirCos[2][1]);
  LPSDirCos[2][2] = (LPSDirCos[0][0]*LPSDirCos[1][1]-LPSDirCos[1][0]*LPSDirCos[0][1]);

  std::cout << "ImageOrientationPatient (0020:0037): ";
  std::cout << "LPS Orientation Matrix" << std::endl;
  std::cout << LPSDirCos << std::endl;

  itk::Matrix<double,3,3> SpacingMatrix;
  SpacingMatrix.Fill(0.0);
  SpacingMatrix[0][0]=xRes;
  SpacingMatrix[1][1]=yRes;
  SpacingMatrix[2][2]=sliceSpacing;
  std::cout << "SpacingMatrix" << std::endl;
  std::cout << SpacingMatrix << std::endl;

  itk::Matrix<double,3,3> OrientationMatrix;
  OrientationMatrix.SetIdentity();

  itk::Matrix<double,3,3> NRRDSpaceDirection;
  std::string nrrdSpaceDefinition("");
  if ( vendor.find("SIEMENS") != std::string::npos || vendor.find("PHILIPS") != std::string::npos )
    {
    nrrdSpaceDefinition="left-posterior-superior";
    }
  else if ( vendor.find("GE") != std::string::npos && !useLPS )
    {
    nrrdSpaceDefinition="right-anterior-superior";
    OrientationMatrix[0][0]=-1;
    OrientationMatrix[1][1]=-1;
    OrientationMatrix[2][2]=1;
    ImageOrigin=OrientationMatrix*ImageOrigin;
    }
  else // for all other vendors assume LPS convention
       // for GE, we may force LPS by using "useLPS"
  {
    nrrdSpaceDefinition="left-posterior-superior";
  }
  NRRDSpaceDirection=LPSDirCos*OrientationMatrix*SpacingMatrix;

  std::cout << "NRRDSpaceDirection" << std::endl;
  std::cout << NRRDSpaceDirection << std::endl;

  unsigned int mMosaic = 0;   // number of raws in each mosaic block;
  unsigned int nMosaic = 0;   // number of columns in each mosaic block
  unsigned int nSliceInVolume = 0;
  unsigned int nVolume = 0;

  // figure out slice order and mosaic arrangement.
  if ( vendor.find("GE") != std::string::npos ||
    (vendor.find("SIEMENS") != std::string::npos && !SliceMosaic) )
    {
    if(vendor.find("GE") != std::string::npos)
      {
      MeasurementFrame=LPSDirCos;
      }
    else //SIEMENS data assumes a measurement frame that is the identity matrix.
      {
      MeasurementFrame.SetIdentity();
      }
    // has the measurement frame represented as an identity matrix.
      ExtractBinValEntry( allHeaders[0], 0x0020, 0x0032, tag );
      float x0, y0, z0;
    sscanf( tag.c_str(), "%f\\%f\\%f", &x0, &y0, &z0 );
    std::cout << "Slice 0: " << tag << std::endl;

    // assume volume interleaving, i.e. the second dicom file stores
    // the second slice in the same volume as the first dicom file
      ExtractBinValEntry( allHeaders[1], 0x0020, 0x0032, tag );
    float x1, y1, z1;
    sscanf( tag.c_str(), "%f\\%f\\%f", &x1, &y1, &z1 );
    std::cout << "Slice 1: " << tag << std::endl;
    x1 -= x0; y1 -= y0; z1 -= z0;
    x0 = x1*(NRRDSpaceDirection[0][2]) + y1*(NRRDSpaceDirection[1][2]) + z1*(NRRDSpaceDirection[2][2]);
    if (x0 < 0)
      {
      SliceOrderIS = false;
      }
    }
  else if ( vendor.find("SIEMENS") != std::string::npos && SliceMosaic )
    {
    MeasurementFrame.SetIdentity(); //The DICOM version of SIEMENS that uses private tags
    // has the measurement frame represented as an identity matrix.
    std::cout << "Siemens SliceMosaic......" << std::endl;

    SliceOrderIS = false;

    // for siemens mosaic image, figure out mosaic slice order from 0029|1010
    // copy information stored in 0029,1010 into a string for parsing
    ExtractBinValEntry( allHeaders[0], 0x0029, 0x1010, tag );

    // parse SliceNormalVector from 0029,1010 tag
    std::vector<double> valueArray(0);
    int nItems = ExtractSiemensDiffusionInformation(tag, "SliceNormalVector", valueArray);
    if (nItems != 3)  // did not find enough information
      {
      std::cout << "Warning: Cannot find complete information on SliceNormalVector in 0029|1010\n";
      std::cout << "         Slice order may be wrong.\n";
      }
    else if (valueArray[2] > 0)
      {
      SliceOrderIS = true;
      }

    // parse NumberOfImagesInMosaic from 0029,1010 tag
    valueArray.resize(0);
    nItems = ExtractSiemensDiffusionInformation(tag, "NumberOfImagesInMosaic", valueArray);
    if (nItems == 0)  // did not find enough information
      {
      std::cout << "Warning: Cannot find complete information on NumberOfImagesInMosaic in 0029|1010\n";
      std::cout << "         Resulting image may contain empty slices.\n";
      }
    else
      {
      nSliceInVolume = static_cast<int>(valueArray[0]);
      mMosaic = static_cast<int> (ceil(sqrt(valueArray[0])));
      nMosaic = mMosaic;
      }
    std::cout << "Mosaic in " << mMosaic << " X " << nMosaic << " blocks (total number of blocks = " << valueArray[0] << ").\n";
    }
  else if ( vendor.find("PHILIPS") != std::string::npos 
    && nSlice > 1) // so this is not a philips multi-frame single dicom file
    {
    MeasurementFrame=LPSDirCos; //Philips oblique scans list the gradients with respect to the ImagePatientOrientation.
    SliceOrderIS = true;

    nSliceInVolume = numberOfSlicesPerVolume;
    nVolume = nSlice/nSliceInVolume;

    float x0, y0, z0;
    float x1, y1, z1;
    ExtractBinValEntry( allHeaders[0], 0x0020, 0x0032, tag );
    sscanf( tag.c_str(), "%f\\%f\\%f", &x0, &y0, &z0 );
    std::cout << "Slice 0: " << tag << std::endl;

    // b-value volume interleaving
    ExtractBinValEntry( allHeaders[nVolume], 0x0020, 0x0032, tag );
    sscanf( tag.c_str(), "%f\\%f\\%f", &x1, &y1, &z1 );
    std::cout << "Slice 1: " << tag << std::endl;
    x1 -= x0; y1 -= y0; z1 -= z0;
    x0 = x1*(NRRDSpaceDirection[0][2]) + y1*(NRRDSpaceDirection[1][2]) + z1*(NRRDSpaceDirection[2][2]);

    // VAM - This needs more investigation -
    // Should we default to false and change based on slice order
    if (x0 < 0)
      {
      SliceOrderIS = false;
      }
    }
  else if ( vendor.find("PHILIPS") != std::string::npos 
    && nSlice == 1)
  {
    // special handling for philips multi-frame dicom later. 
  }
  else
    {
    std::cout << " ERROR vendor type not valid" << std::endl;
    exit(-1);
    }

  if ( SliceOrderIS )
    {
    std::cout << "Slice order is IS\n";
    }
  else
    {
    std::cout << "Slice order is SI\n";
    (NRRDSpaceDirection[0][2]) = -(NRRDSpaceDirection[0][2]);
    (NRRDSpaceDirection[1][2]) = -(NRRDSpaceDirection[1][2]);
    (NRRDSpaceDirection[2][2]) = -(NRRDSpaceDirection[2][2]);
    }

  std::cout << "Row: " << (NRRDSpaceDirection[0][0]) << ", " << (NRRDSpaceDirection[1][0]) << ", " << (NRRDSpaceDirection[2][0]) << std::endl;
  std::cout << "Col: " << (NRRDSpaceDirection[0][1]) << ", " << (NRRDSpaceDirection[1][1]) << ", " << (NRRDSpaceDirection[2][1]) << std::endl;
  std::cout << "Sli: " << (NRRDSpaceDirection[0][2]) << ", " << (NRRDSpaceDirection[1][2]) << ", " << (NRRDSpaceDirection[2][2]) << std::endl;

  const float orthoSliceSpacing = fabs((NRRDSpaceDirection[2][2]));

  int nIgnoreVolume = 0; // Used for Philips Trace like images
  std::vector<int> useVolume;

  std::vector<float> bValues(0);
  float maxBvalue = 0;
  int nBaseline = 0;

  // UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem is only of debug purposes.
  std::vector< vnl_vector_fixed<double, 3> > DiffusionVectors;
  std::vector< vnl_vector_fixed<double, 3> > UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem;
  ////////////////////////////////////////////////////////////
  // vendor dependent tags.
  // read in gradient vectors and determin nBaseline and nMeasurement

  if ( vendor.find("GE") != std::string::npos )
    {
    nSliceInVolume = numberOfSlicesPerVolume;
    nVolume = nSlice/nSliceInVolume;

    // assume volume interleaving
    std::cout << "Number of Slices: " << nSlice << std::endl;
    std::cout << "Number of Volume: " << nVolume << std::endl;
    std::cout << "Number of Slices in each volume: " << nSliceInVolume << std::endl;

    for (unsigned int k = 0; k < nSlice; k += nSliceInVolume)
      {
      // parsing bvalue and gradient directions
      vnl_vector_fixed<double, 3> vect3d;
      vect3d.fill( 0 );
      float b = 0;

      ExtractBinValEntry( allHeaders[k], 0x0043, 0x1039, tag );
      b = atof( tag.c_str() );

      ExtractBinValEntry( allHeaders[k], 0x0019, 0x10bb, tag );
      vect3d[0] = atof( tag.c_str() );

      ExtractBinValEntry( allHeaders[k], 0x0019, 0x10bc, tag );
      vect3d[1] = atof( tag.c_str() );

      ExtractBinValEntry( allHeaders[k], 0x0019, 0x10bd, tag );
      vect3d[2] = atof( tag.c_str() );

      bValues.push_back( b );
      if (b == 0)
        {
        vect3d.fill( 0 );
        UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem.push_back(vect3d);
        DiffusionVectors.push_back(vect3d);
        }
      else
        {
        UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem.push_back(vect3d);
        vect3d.normalize();
        DiffusionVectors.push_back(vect3d);
        }

      std::cout << "B-value: " << b << 
        "; diffusion direction: " << vect3d[0] << ", " << vect3d[1] << ", " << vect3d[2] << std::endl;
      }
    }
  else if ( vendor.find("PHILIPS") != std::string::npos && nSlice > 1 )
    {
    // assume volume interleaving
    std::cout << "Number of Slices: " << nSlice << std::endl;
    std::cout << "Number of Volumes: " << nVolume << std::endl;
    std::cout << "Number of Slices in each volume: " << nSliceInVolume << std::endl;

    //NOTE:  Philips interleaves the directions, so the all gradient directions can be
    //determined in the first "nVolume" slices which represents the first slice from each
    //of the gradient volumes.
    for (unsigned int k = 0; k < nVolume; k++ /*nSliceInVolume*/)
      {
      const bool useSuppplement49Definitions = ExtractBinValEntry( allHeaders[k], 0x0018, 0x9075, tag );
      std::string DiffusionDirectionality(tag);//This is either NONE or DIRECTIONAL (or ISOTROPIC or "" if the tag is not found)

      bool B0FieldFound = false;
      float b=0.0;
      if (useSuppplement49Definitions == true )
      {
        B0FieldFound=ExtractBinValEntry( allHeaders[k], 0x0018, 0x9087, tag );;
        double temp_b=0.0;
        memcpy(&temp_b,tag.c_str(),8);//This seems very strange to me, it should have been a binary value.
        b=temp_b;
      }
      else
      {
        B0FieldFound=ExtractBinValEntry( allHeaders[k], 0x2001, 0x1003, tag );
        //HJJ -- VAM -- HACK:  This indicates that this field is store in binaryj format.
        //it seems that this would not work consistently across differnt endedness machines.
        memcpy(&b, tag.c_str(), 4);
        ExtractBinValEntry( allHeaders[k], 0x2001, 0x1004, tag );
        if((tag.find("I") != std::string::npos) && (b != 0) )
        {
          DiffusionDirectionality="ISOTROPIC";
        }
        // char const * const temp=tag.c_str();
      }

      vnl_vector_fixed<double, 3> vect3d;
      vect3d.fill( 0 );
      //std::cout << "HACK: " << DiffusionDirectionality << "  " <<  k << std::endl;
      //std::cout << "HACK: " << B0FieldFound << " " << b << " " << DiffusionDirectionality << std::endl;
      if ( DiffusionDirectionality.find("ISOTROPIC") != std::string::npos )
        { //Deal with images that are to be ignored
        //std::cout << " SKIPPING ISOTROPIC Diffusion. " << std::endl;
        //std::cout << "HACK: IGNORE IMAGEFILE:   " << k << " of " << filenames.size() << " " << filenames[k] << std::endl;
        // Ignore the Trace like image
        nIgnoreVolume++;
        useVolume.push_back(0);
        continue;
        }
      else if (( !B0FieldFound || b == 0 ) || ( DiffusionDirectionality.find("NONE") != std::string::npos )   )
        { //Deal with b0 images
        bValues.push_back(b);
        UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem.push_back(vect3d);
        DiffusionVectors.push_back(vect3d);
        useVolume.push_back(1);
        continue;
        }
      else
        { //Deal with gradient direction images
        bValues.push_back(b);
        assert(DiffusionDirectionality.find("DIRECTIONAL") != std::string::npos);
        //std::cout << "HACK: GRADIENT IMAGEFILE: " << k << " of " << filenames.size() << " " << filenames[k] << std::endl;
        useVolume.push_back(1);
        tag.clear();
        if (useSuppplement49Definitions == true )
          {
          //Use alternate method to get value out of a sequence header (Some Phillips Data).
          const bool exposedSuccedded=ExtractBinValEntry( allHeaders[k], 0x0018, 0x9089, tag );
          if(exposedSuccedded == false)
            {
            //std::cout << "Looking for  0018|9089 in sequence 0018,9076" << std::endl;

            gdcm::SeqEntry * DiffusionSeqEntry=allHeaders[k]->GetSeqEntry(0x0018,0x9076);
            if(DiffusionSeqEntry == NULL)
              {
              std::cout << "ERROR:  0018|9089 could not be found in Seq Entry 0018|9076" << std::endl;
              exit(-1);
              }
            else
              {
              const unsigned int n=DiffusionSeqEntry->GetNumberOfSQItems();
              if( n == 0 )
                {
                std::cout << "ERROR:  Sequence entry 0018|9076 has no items." << std::endl;
                exit(-1);
                }
              else
                {
                bool found0018_9089=false;
                gdcm::SQItem *item1=DiffusionSeqEntry->GetFirstSQItem();
                while(item1 != NULL && found0018_9089==false)
                  {
                  if( item1 == NULL )
                    {
                    std::cout << "ERROR:  First item not found for 0018|9076." << std::endl;
                    exit(-1);
                    }
                  else
                    {
                    gdcm::DocEntry *p=item1->GetDocEntry(0x0018,0x9089);
                    if(p==NULL)
                      {
                      item1=DiffusionSeqEntry->GetNextSQItem();
                      continue;
                      }
                    else
                      {
                      gdcm::ContentEntry *entry=dynamic_cast<gdcm::ContentEntry *>(p);
                      if(entry ==NULL)
                        {
                        std::cout << "ERROR:  DocEntry for 0018|9089 is not the correct type." << std::endl;
                        exit(-1);
                        }
                      else
                        {
                        found0018_9089=true;
                        const std::string gradientDirection(entry->GetValue());
                        //double x[3];
                        //memcpy(x,gradientDirection.c_str(),24);
                        //std::cout << "################################# [" << x[0] << ","<< x[1] << "," << x[2] << "]" << std::endl;
                        memcpy(&(vect3d[0]),gradientDirection.c_str(),24);
                        }
                      }
                    }
                  item1=DiffusionSeqEntry->GetNextSQItem();
                  }
                if(found0018_9089 == false)
                  {
                  std::cout << "ERROR:  0018|9076 sequence not found." << std::endl;
                  exit(-1);
                  }
                }
              }
            }
          else
            {
            itksysBase64_Decode((const unsigned char *)(tag.c_str()), 8*3, (unsigned char *)(&(vect3d[0])),8*3*4);
            std::cout << "===== gradient orientations:" << k << " " << filenames[k] << " (0018,9089) " << " " << *((const double * )tag.c_str()) << "|"<<vect3d << std::endl;
            }
          }
        else
          {
          float value;
          /*const bool b0exist =*/
          ExtractBinValEntry( allHeaders[k], 0x2005, 0x10b0, tag );
          memcpy(&value, tag.c_str(), 4); //HACK:  VAM -- this does not seem to be endedness compliant.
          vect3d[0] = value;
          tag.clear();

          /*const bool b1exist =*/
          ExtractBinValEntry( allHeaders[k], 0x2005, 0x10b1, tag );
          memcpy(&value, tag.c_str(), 4); //HACK:  VAM -- this does not seem to be endedness compliant.
          vect3d[1] = value;
          tag.clear();

          /*const bool b2exist =*/
          ExtractBinValEntry( allHeaders[k], 0x2005, 0x10b2, tag );
          memcpy(&value, tag.c_str(), 4); //HACK:  VAM -- this does not seem to be endedness compliant.
          vect3d[2] = value;
          }

        UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem.push_back(vect3d);
        vect3d.normalize();
        DiffusionVectors.push_back(vect3d);
        }
      std::cout << "DiffusionDirectionality: " << DiffusionDirectionality  << " :B-Value " << b << " :DiffusionOrientation " << vect3d << " :Filename " << filenames[k] << std::endl;
      }
    }
  else if ( vendor.find("SIEMENS") != std::string::npos )
    {

    int nStride = 1;
    if ( !SliceMosaic )
      {
      std::cout << orthoSliceSpacing << std::endl;
      nSliceInVolume = numberOfSlicesPerVolume;
      nVolume = nSlice/nSliceInVolume;
      std::cout << "Number of Slices: " << nSlice << std::endl;
      std::cout << "Number of Volume: " << nVolume << std::endl;
      std::cout << "Number of Slices in each volume: " << nSliceInVolume << std::endl;
      nStride = nSliceInVolume;
      }
    else
      {
      std::cout << "Data in Siemens Mosaic Format\n";
      nVolume = nSlice;
      std::cout << "Number of Volume: " << nVolume << std::endl;
      std::cout << "Number of Slices in each volume: " << nSliceInVolume << std::endl;
      nStride = 1;
      }

    for (unsigned int k = 0; k < nSlice; k += nStride )
      {

      ExtractBinValEntry( allHeaders[k], 0x0029, 0x1010, tag );

      // parse B_value from 0029,1010 tag
      std::vector<double> valueArray(0);
      vnl_vector_fixed<double, 3> vect3d;
      int nItems = ExtractSiemensDiffusionInformation(tag, "B_value", valueArray);
      std::cout << "Number of Items for Bvalue : " << nItems << std::endl;
      std::cout << "Bvalue : " << valueArray[0] << std::endl;

      if (nItems != 1)   // did not find enough information
        {
        std::cout << "Warning: Cannot find complete information on B_value in 0029|1010\n";
        bValues.push_back( 0.0 );
        vect3d.fill( 0.0 );
        UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem.push_back(vect3d);
        DiffusionVectors.push_back(vect3d);
        continue;
        }
      else
        {
        bValues.push_back( valueArray[0] );
        }

      // parse DiffusionGradientDirection from 0029,1010 tag
      valueArray.resize(0);
      nItems = ExtractSiemensDiffusionInformation(tag, "DiffusionGradientDirection", valueArray);
      std::cout << "Number of Directions : " << nItems << std::endl;
      std::cout << "   Directions 0: " << valueArray[0] << std::endl;
      std::cout << "   Directions 1: " << valueArray[1] << std::endl;
      std::cout << "   Directions 2: " << valueArray[2] << std::endl;
      if (nItems != 3)  // did not find enough information
        {
        std::cout << "Warning: Cannot find complete information on DiffusionGradientDirection in 0029|1010\n";
        vect3d.fill( 0 );
        UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem.push_back(vect3d);
        DiffusionVectors.push_back(vect3d);
        }
      else
        {
        vect3d[0] = valueArray[0];
        vect3d[1] = valueArray[1];
        vect3d[2] = valueArray[2];
        UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem.push_back(vect3d);
        vect3d.normalize();
        DiffusionVectors.push_back(vect3d);
        int p = bValues.size();
        std::cout << "Image#: " << k << " BV: " << bValues[p-1] << " GD: " << DiffusionVectors[p-1] << std::endl;
        }
      }
    }
  else if (vendor.find("PHILIPS") != std::string::npos && nSlice == 1) // multi-frame file, everything is inside
  {

    std::map<std::vector<double>, double> gradientDirectionAndBValue;
    ignorePhilipsSliceMultiFrame.clear();
    sliceLocations.clear();
    gdcm::File *header = new gdcm::File;
    header->SetMaxSizeLoadEntry(65535);
    header->SetFileName( filenames[0] );
    header->SetLoadMode( gdcm::LD_ALL );
    header->Load();
    gdcm::DocEntry* d = header->GetFirstEntry();

    bValues.clear();
    DiffusionVectors.clear();
    useVolume.clear();

    bool visited00280010 = false;
    bool visited00280011 = false;
    bool visited00180088 = false;
    while(d)
    {
      if (d->GetKey() == "0028|0010")
      {
        visited00280010 = true;
        gdcm::ValEntry* v = dynamic_cast<gdcm::ValEntry*> (d);
        d->Print( std::cout );
        nRows = atoi(v->GetValue().c_str());
      }
      else if (d->GetKey() == "0028|0011")
      {
        visited00280011 = true;
        gdcm::ValEntry* v = dynamic_cast<gdcm::ValEntry*> (d);
        d->Print( std::cout );
        nCols = atoi(v->GetValue().c_str());
      }
      else if (d->GetKey() == "0018|0088")
      {
        visited00180088 = true;
        gdcm::ValEntry* v = dynamic_cast<gdcm::ValEntry*> (d);
        d->Print( std::cout );
        sliceSpacing = atof(v->GetValue().c_str());
      }
      else if (d->GetKey() == "5200|9230" && visited00180088 && visited00280011 && visited00280010)
      {
        break;
      }
      d = header->GetNextEntry();
    }

    gdcm::SeqEntry* sq = dynamic_cast<gdcm::SeqEntry*> (d);
    int nItems = sq->GetNumberOfSQItems();
    std::cout << "Total number of slices: " << nItems << std::endl;
    gdcm::SQItem * sqi = sq->GetFirstSQItem();

    // figure out 
    // 1. size
    // 2. space directions
    // 3. space origin
    // 4. measurement frame
    // 5. slice order (SI or IS)
    int k = 0;
    while (sqi)
    {

      gdcm::SeqEntry* volEntry;
      gdcm::SQItem * innerSqi;
      gdcm::ValEntry* valEntry; 

      if ( k == 0 )
      {
        volEntry = dynamic_cast<gdcm::SeqEntry*>( sqi->GetDocEntry( 0x0020, 0x9116) );
        innerSqi = volEntry->GetFirstSQItem();
        valEntry = dynamic_cast<gdcm::ValEntry*>( innerSqi->GetDocEntry( 0x0020, 0x0037) );
        sscanf( valEntry->GetValue().c_str(), "%lf\\%lf\\%lf\\%lf\\%lf\\%lf",
          &(LPSDirCos[0][0]), &(LPSDirCos[1][0]), &(LPSDirCos[2][0]),
          &(LPSDirCos[0][1]), &(LPSDirCos[1][1]), &(LPSDirCos[2][1]) );
        // Cross product, this gives I-axis direction
        LPSDirCos[0][2] = (LPSDirCos[1][0]*LPSDirCos[2][1]-LPSDirCos[2][0]*LPSDirCos[1][1]);
        LPSDirCos[1][2] = (LPSDirCos[2][0]*LPSDirCos[0][1]-LPSDirCos[0][0]*LPSDirCos[2][1]);
        LPSDirCos[2][2] = (LPSDirCos[0][0]*LPSDirCos[1][1]-LPSDirCos[1][0]*LPSDirCos[0][1]);

        volEntry = dynamic_cast<gdcm::SeqEntry*>( sqi->GetDocEntry( 0x0028, 0x9110) );
        innerSqi = volEntry->GetFirstSQItem();
        valEntry = dynamic_cast<gdcm::ValEntry*>( innerSqi->GetDocEntry( 0x0028, 0x0030) );
        sscanf( valEntry->GetValue().c_str(), "%f\\%f", &xRes, &yRes );
      }

      volEntry = dynamic_cast<gdcm::SeqEntry*>( sqi->GetDocEntry( 0x0020, 0x9113) );
      innerSqi = volEntry->GetFirstSQItem();
      valEntry = dynamic_cast<gdcm::ValEntry*>( innerSqi->GetDocEntry( 0x0020, 0x0032) );
      sliceLocations[valEntry->GetValue()] ++;
      if ( k == 0 )
      {
        sscanf( valEntry->GetValue().c_str(), "%lf\\%lf\\%lf",  &(ImageOrigin[0]), &(ImageOrigin[1]), &(ImageOrigin[2]) );
      }

      // figure out diffusion directions
      volEntry = dynamic_cast<gdcm::SeqEntry*>( sqi->GetDocEntry( 0x0018, 0x9117) );
      innerSqi = volEntry->GetFirstSQItem();
      valEntry = dynamic_cast<gdcm::ValEntry*>( innerSqi->GetDocEntry( 0x0018, 0x9075) );
      std::string dirValue = valEntry->GetValue();

      if ( dirValue.find("ISO") != std::string::npos )
      {
        useVolume.push_back(0);
        ignorePhilipsSliceMultiFrame.push_back( k );
      }
      else if (dirValue.find("NONE") != std::string::npos)
      {
        useVolume.push_back(1);
        std::vector<double> v(3);
        v[0] = 0; v[1] = 0; v[2] = 0;
        unsigned int nOld = gradientDirectionAndBValue.size();
        gradientDirectionAndBValue[v] = 0;
        unsigned int nNew = gradientDirectionAndBValue.size();

        if (nOld != nNew)
        {
          vnl_vector_fixed<double, 3> vect3d;
          vect3d.fill( 0 );
          DiffusionVectors.push_back( vect3d );
          UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem.push_back(vect3d);
          bValues.push_back( 0 );

        }
      }
      else
      {
        useVolume.push_back(1);
        valEntry = dynamic_cast<gdcm::ValEntry*>( innerSqi->GetDocEntry( 0x0018, 0x9087) );
        std::string dwbValue = valEntry->GetValue();

        volEntry = dynamic_cast<gdcm::SeqEntry*>( innerSqi->GetDocEntry( 0x0018, 0x9076) );
        innerSqi = volEntry->GetFirstSQItem();
        valEntry = dynamic_cast<gdcm::ValEntry*>( innerSqi->GetDocEntry( 0x0018, 0x9089) );
        std::string dwgValue = valEntry->GetValue();
        std::vector<double> v(3);
        v[0] = *(double*)(dwgValue.c_str());
        v[1] = *(double*)(dwgValue.c_str()+8);
        v[2] = *(double*)(dwgValue.c_str()+16);
        unsigned int nOld = gradientDirectionAndBValue.size();
        gradientDirectionAndBValue[v] = *(double*)(dwbValue.c_str());
        unsigned int nNew = gradientDirectionAndBValue.size();

        if (nOld != nNew)
        {
          vnl_vector_fixed<double, 3> vect3d;
          vect3d[0] = v[0]; vect3d[1] = v[1]; vect3d[2] = v[2];
          UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem.push_back(vect3d);
          vect3d.normalize();
          DiffusionVectors.push_back( vect3d );

          bValues.push_back( *(double*)(dwbValue.c_str()) );
        }
      }

      sqi = sq->GetNextSQItem();
      k ++;
    }
    numberOfSlicesPerVolume=sliceLocations.size();
    std::cout << "LPS Matrix: \n" << LPSDirCos << std::endl;
    std::cout << "Volume Origin: \n" << ImageOrigin[0] << "," << ImageOrigin[1] << ","  << ImageOrigin[2] << "," << std::endl;
    std::cout << "Number of slices per volume: " << numberOfSlicesPerVolume << std::endl;
    std::cout << "Slice matrix size: " << nRows << " X " << nCols << std::endl;
    std::cout << "Image resolution: " << xRes << ", " << yRes << ", " << sliceSpacing << std::endl;

    SpacingMatrix.Fill(0.0);
    SpacingMatrix[0][0]=xRes;
    SpacingMatrix[1][1]=yRes;
    SpacingMatrix[2][2]=sliceSpacing;
    NRRDSpaceDirection=LPSDirCos*OrientationMatrix*SpacingMatrix;

    MeasurementFrame=LPSDirCos;

    nSliceInVolume = sliceLocations.size();
    nVolume = nItems/nSliceInVolume;
    nIgnoreVolume = ignorePhilipsSliceMultiFrame.size()/nSliceInVolume;

    for( unsigned int k = 0; k < bValues.size(); k++ ) 
    {
      std::cout << k << ": direction: " <<  DiffusionVectors[k][0] << ", " << DiffusionVectors[k][1] << ", " << DiffusionVectors[k][2] << ", b-value: " << bValues[k] << std::endl;
    }

  }
  else
    {
    std::cout << "ERROR: Unknown scanner vendor " << vendor << std::endl;
    std::cout << "       this dti file format is properly handled." << std::endl;
    exit(-1);
    }


  ///////////////////////////////////////////////
  // write volumes in raw format
  itk::ImageFileWriter< VolumeType >::Pointer rawWriter = itk::ImageFileWriter< VolumeType >::New();
  itk::RawImageIO<PixelValueType, 3>::Pointer rawIO = itk::RawImageIO<PixelValueType, 3>::New();
  //std::string rawFileName = outputDir + "/" + dataname;
  rawWriter->SetFileName( dataname.c_str() );
  rawWriter->SetImageIO( rawIO );
  rawIO->SetByteOrderToLittleEndian();

  ///////////////////////////////////////////////
  // Update the number of volumes based on the
  // number to ignore from the header information
  const unsigned int nUsableVolumes = nVolume-nIgnoreVolume;
  std::cout << "Number of usable volumes: " << nUsableVolumes << std::endl;

  if ( vendor.find("GE") != std::string::npos ||
    (vendor.find("SIEMENS") != std::string::npos && !SliceMosaic) )
    {
    rawWriter->SetInput( reader->GetOutput() );
    try
      {
      rawWriter->Update();
      }
    catch (itk::ExceptionObject &excp)
      {
      std::cerr << "Exception thrown while reading the series" << std::endl;
      std::cerr << excp << std::endl;
      return EXIT_FAILURE;
      }
    }
  else if ( vendor.find("SIEMENS") != std::string::npos && SliceMosaic)
    {
    // de-mosaic
    nRows /= mMosaic;
    nCols /= nMosaic;

    // center the volume since the image position patient given in the
    // dicom header was useless
    ImageOrigin[0] = -(nRows*(NRRDSpaceDirection[0][0]) + nCols*(NRRDSpaceDirection[0][1]) + nSliceInVolume*(NRRDSpaceDirection[0][2]))/2.0;
    ImageOrigin[1] = -(nRows*(NRRDSpaceDirection[1][0]) + nCols*(NRRDSpaceDirection[1][1]) + nSliceInVolume*(NRRDSpaceDirection[1][2]))/2.0;
    ImageOrigin[2] = -(nRows*(NRRDSpaceDirection[2][0]) + nCols*(NRRDSpaceDirection[2][1]) + nSliceInVolume*(NRRDSpaceDirection[2][2]))/2.0;

    VolumeType::Pointer img = reader->GetOutput();

    VolumeType::RegionType region = img->GetLargestPossibleRegion();
    VolumeType::SizeType size = region.GetSize();

    VolumeType::SizeType dmSize = size;
    dmSize[0] /= mMosaic;
    dmSize[1] /= nMosaic;
    dmSize[2] *= nSliceInVolume;

    region.SetSize( dmSize );
    VolumeType::Pointer dmImage = VolumeType::New();
    dmImage->CopyInformation( img );
    dmImage->SetRegions( region );
    dmImage->Allocate();

    VolumeType::RegionType dmRegion = dmImage->GetLargestPossibleRegion();
    dmRegion.SetSize(2, 1);
    region.SetSize(0, dmSize[0]);
    region.SetSize(1, dmSize[1]);
    region.SetSize(2, 1);

    //    int rawMosaic = 0;
    //    int colMosaic = 0;

    for (unsigned int k = 0; k < dmSize[2]; k++)
      {
      dmRegion.SetIndex(2, k);
      itk::ImageRegionIteratorWithIndex<VolumeType> dmIt( dmImage, dmRegion );

      // figure out the mosaic region for this slice
      int sliceIndex = k;

      //int nBlockPerSlice = mMosaic*nMosaic;
      int slcMosaic = sliceIndex/(nSliceInVolume);
      sliceIndex -= slcMosaic*nSliceInVolume;
      int colMosaic = sliceIndex/mMosaic;
      int rawMosaic = sliceIndex - mMosaic*colMosaic;
      region.SetIndex( 0, rawMosaic*dmSize[0] );
      region.SetIndex( 1, colMosaic*dmSize[1] );
      region.SetIndex( 2, slcMosaic );

      itk::ImageRegionConstIteratorWithIndex<VolumeType> imIt( img, region );

      for ( dmIt.GoToBegin(), imIt.GoToBegin(); !dmIt.IsAtEnd(); ++dmIt, ++imIt)
        {
        dmIt.Set( imIt.Get() );
        }
      }
    rawWriter->SetInput( dmImage );
    try
      {
      rawWriter->Update();
      }
    catch (itk::ExceptionObject &excp)
      {
      std::cerr << "Exception thrown while writing the series" << std::endl;
      std::cerr << excp << std::endl;
      return EXIT_FAILURE;
      }
    }
  else if (vendor.find("PHILIPS") != std::string::npos)
    {
    VolumeType::Pointer img = reader->GetOutput();

    VolumeType::RegionType region = img->GetLargestPossibleRegion();
    VolumeType::SizeType size = region.GetSize();

    VolumeType::SizeType dmSize = size;
    dmSize[2] = nSliceInVolume * (nUsableVolumes);

    region.SetSize( dmSize );
    VolumeType::Pointer dmImage = VolumeType::New();
    dmImage->CopyInformation( img );
    dmImage->SetRegions( region );
    dmImage->Allocate();

    VolumeType::RegionType dmRegion = dmImage->GetLargestPossibleRegion();
    dmRegion.SetSize(2, 1);
    region.SetSize(0, dmSize[0]);
    region.SetSize(1, dmSize[1]);
    region.SetSize(2, 1);

    unsigned int count = 0;
    for (unsigned int i = 0; i < nVolume; i++)
      {
      if (useVolume[i] == 1)
        {
        for (unsigned int k = 0; k < nSliceInVolume; k++)
          {
          dmRegion.SetIndex(0, 0);
          dmRegion.SetIndex(1, 0);
          dmRegion.SetIndex(2, count*(nSliceInVolume)+k);
          itk::ImageRegionIteratorWithIndex<VolumeType> dmIt( dmImage, dmRegion );

          // figure out the region for this slice
          const int sliceIndex = k*nVolume+i;
          region.SetIndex( 0, 0 );
          region.SetIndex( 1, 0 );
          region.SetIndex( 2, sliceIndex );

          itk::ImageRegionConstIteratorWithIndex<VolumeType> imIt( img, region );

          for ( dmIt.GoToBegin(), imIt.GoToBegin(); !dmIt.IsAtEnd(); ++dmIt, ++imIt)
            {
            dmIt.Set( imIt.Get() );
            }
          }
        count++;
        }
      }
    rawWriter->SetInput( dmImage );
    try
      {
      rawWriter->Update();
      }
    catch (itk::ExceptionObject &excp)
      {
      std::cerr << "Exception thrown while writing the series" << std::endl;
      std::cerr << excp << std::endl;
      return EXIT_FAILURE;
      }
    //Verify sizes
    if( count != bValues.size() )
      {
      std::cout << "ERROR:  bValues are the wrong size." <<  count << " != " << bValues.size() << std::endl;
      exit(-1);
      }
    if( count != DiffusionVectors.size() )
      {
      std::cout << "ERROR:  DiffusionVectors are the wrong size." <<  count << " != " << DiffusionVectors.size() << std::endl;
      exit(-1);
      }
    if( count != UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem.size() )
      {
      std::cout << "ERROR:  UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem are the wrong size." <<  count << " != " << UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem.size() << std::endl;
      exit(-1);
      }
    }
  else
    {
    std::cout << "ERROR:  invalid vendor found." << std::endl;
    exit(-1);
    }


    const vnl_matrix_fixed<double,3,3> InverseMeasurementFrame= MeasurementFrame.GetInverse();
    {
    //////////////////////////////////////////////
    // write header file
    // This part follows a DWI NRRD file in NRRD format 5.
    // There should be a better way using itkNRRDImageIO.

    std::ofstream header;
    //std::string headerFileName = outputDir + "/" + outputFileName;

    header.open (nhdrname.c_str());
    header << "NRRD0005" << std::endl;

    header << "content: exists(" << itksys::SystemTools::GetFilenameName(dataname) << ",0)" << std::endl;
    header << "type: short" << std::endl;
    header << "dimension: 4" << std::endl;

    // need to check
    header << "space: " << nrrdSpaceDefinition << "" << std::endl;
    // in nrrd, size array is the number of pixels in 1st, 2nd, 3rd, ... dimensions
    header << "sizes: " << nCols << " " << nRows << " " << nSliceInVolume << " " << nUsableVolumes << std::endl;
    header << "thicknesses:  NaN  NaN " << sliceSpacing << " NaN" << std::endl;

    // need to check
    header << "space directions: "
      << "(" << (NRRDSpaceDirection[0][0]) << ","<< (NRRDSpaceDirection[1][0]) << ","<< (NRRDSpaceDirection[2][0]) << ") "
      << "(" << (NRRDSpaceDirection[0][1]) << ","<< (NRRDSpaceDirection[1][1]) << ","<< (NRRDSpaceDirection[2][1]) << ") "
      << "(" << (NRRDSpaceDirection[0][2]) << ","<< (NRRDSpaceDirection[1][2]) << ","<< (NRRDSpaceDirection[2][2]) 
      << ") none" << std::endl;
    header << "centerings: cell cell cell ???" << std::endl;
    header << "kinds: space space space list" << std::endl;

    header << "endian: little" << std::endl;
    header << "encoding: raw" << std::endl;
    header << "space units: \"mm\" \"mm\" \"mm\"" << std::endl;
    header << "space origin: "
      <<"(" << ImageOrigin[0] << ","<< ImageOrigin[1] << ","<< ImageOrigin[2] << ") " << std::endl;
    header << "data file: " << itksys::SystemTools::GetFilenameName(dataname) << std::endl;

    // For scanners, the measurement frame for the gradient directions is the same as the 
    // Excerpt from http://teem.sourceforge.net/nrrd/format.html definition of "measurement frame:"
    // There is also the possibility that a measurement frame
    // should be recorded for an image even though it is storing
    // only scalar values (e.g., a sequence of diffusion-weighted MR
    // images has a measurement frame for the coefficients of
    // the diffusion-sensitizing gradient directions, and
    // the measurement frame field is the logical store
    // this information).
    //
    // It was noticed on oblique Philips DTI scans that the prescribed protocol directions were
    // rotated by the ImageOrientationPatient amount and recorded in the DICOM header.
    // In order to compare two different scans to determine if the same protocol was prosribed,
    // it is necessary to multiply each of the recorded diffusion gradient directions by
    // the inverse of the LPSDirCos.
    if(useIdentityMeaseurementFrame)
      {
      header << "measurement frame: "
        << "(" << 1 << ","<< 0 << ","<< 0 << ") "
        << "(" << 0 << ","<< 1 << ","<< 0 << ") "
        << "(" << 0 << ","<< 0 << ","<< 1 << ")"
        << std::endl;
      }
    else
      {
      header << "measurement frame: "
        << "(" << (MeasurementFrame[0][0]) << ","<< (MeasurementFrame[1][0]) << ","<< (MeasurementFrame[2][0]) << ") "
        << "(" << (MeasurementFrame[0][1]) << ","<< (MeasurementFrame[1][1]) << ","<< (MeasurementFrame[2][1]) << ") "
        << "(" << (MeasurementFrame[0][2]) << ","<< (MeasurementFrame[1][2]) << ","<< (MeasurementFrame[2][2]) << ")"
        << std::endl;
      }

    header << "modality:=DWMRI" << std::endl;
    //  float bValue = 0;
    for (unsigned int k = 0; k < nUsableVolumes; k++)
      {
      if (bValues[k] > maxBvalue)
        {
        maxBvalue = bValues[k];
        }
      }

    // this is the norminal BValue, i.e. the largest one.
    header << "DWMRI_b-value:=" << maxBvalue << std::endl;

    //  the following three lines are for older NRRD format, where
    //  baseline images are always in the begining.
    //  header << "DWMRI_gradient_0000:=0  0  0" << std::endl;
    //  header << "DWMRI_NEX_0000:=" << nBaseline << std::endl;
    //  need to check
    for (unsigned int k = 0; k < nUsableVolumes; k++)
      {
      float scaleFactor = 0;
      if (maxBvalue > 0)
        {
        scaleFactor = sqrt( bValues[k]/maxBvalue );
        }
      //std::cout << "For Multiple BValues: " << k << ": " << scaleFactor << std::endl;
      if(useIdentityMeaseurementFrame)
        {
        vnl_vector_fixed<double,3> RotatedDiffusionVectors=InverseMeasurementFrame*(DiffusionVectors[k-nBaseline]);
        header << "DWMRI_gradient_" << std::setw(4) << std::setfill('0') << k << ":="
          << RotatedDiffusionVectors[0] * scaleFactor << "   "
          << RotatedDiffusionVectors[1] * scaleFactor << "   "
          << RotatedDiffusionVectors[2] * scaleFactor << std::endl;
        }
      else
        {
        header << "DWMRI_gradient_" << std::setw(4) << std::setfill('0') << k << ":="
          << DiffusionVectors[k-nBaseline][0] * scaleFactor << "   "
          << DiffusionVectors[k-nBaseline][1] * scaleFactor << "   "
          << DiffusionVectors[k-nBaseline][2] * scaleFactor << std::endl;
        }

      //std::cout << "Consistent Orientation Checks." << std::endl;
      //std::cout << "DWMRI_gradient_" << std::setw(4) << std::setfill('0') << k << ":="
      //  << LPSDirCos.GetInverse()*DiffusionVectors[k-nBaseline] << std::endl;
      }
    header.close();
    }

  if( writeProtocolGradientsFile == true )
    {
    //////////////////////////////////////////////
    // writeProtocolGradientsFile write protocolGradientsFile file
    // This part follows a DWI NRRD file in NRRD format 5.
    // There should be a better way using itkNRRDImageIO.

    std::ofstream protocolGradientsFile;
    //std::string protocolGradientsFileFileName = outputDir + "/" + outputFileName;

    const std::string protocolGradientsFileName=nhdrname+".txt";
    protocolGradientsFile.open ( protocolGradientsFileName.c_str() );
    protocolGradientsFile << "ImageOrientationPatient (0020|0032): "
      << LPSDirCos[0][0] << "\\" << LPSDirCos[1][0] << "\\" << LPSDirCos[2][0] << "\\"
      << LPSDirCos[0][1] << "\\" << LPSDirCos[1][1] << "\\" << LPSDirCos[2][1] << "\\"
      << std::endl;
    protocolGradientsFile << "==================================" << std::endl; 
    protocolGradientsFile << "Direction Cosines: \n" << LPSDirCos << std::endl;
    protocolGradientsFile << "==================================" << std::endl; 
    protocolGradientsFile << "MeasurementFrame: \n" << MeasurementFrame << std::endl;
    protocolGradientsFile << "==================================" << std::endl; 
    for (unsigned int k = 0; k < nUsableVolumes; k++)
      {
      float scaleFactor = 0;
      if (maxBvalue > 0)
        {
        scaleFactor = sqrt( bValues[k]/maxBvalue );
        }
      protocolGradientsFile << "DWMRI_gradient_" << std::setw(4) << std::setfill('0') << k << "=["
        << DiffusionVectors[k-nBaseline][0] * scaleFactor << ";"
        << DiffusionVectors[k-nBaseline][1] * scaleFactor << ";"
        << DiffusionVectors[k-nBaseline][2] * scaleFactor << "]" <<std::endl;
      }
    protocolGradientsFile << "==================================" << std::endl; 
    for (unsigned int k = 0; k < nUsableVolumes; k++)
      {
      float scaleFactor = 0;
      if (maxBvalue > 0)
        {
        scaleFactor = sqrt( bValues[k]/maxBvalue );
        }
      const vnl_vector_fixed<double, 3u> ProtocolGradient= InverseMeasurementFrame*DiffusionVectors[k-nBaseline];
      protocolGradientsFile << "Protocol_gradient_" << std::setw(4) << std::setfill('0') << k << "=["
        << ProtocolGradient[0] * scaleFactor << ";"
        << ProtocolGradient[1] * scaleFactor << ";"
        << ProtocolGradient[2] * scaleFactor << "]" <<std::endl;
      }
    protocolGradientsFile << "==================================" << std::endl; 
    protocolGradientsFile.close();
    }
  return EXIT_SUCCESS;
}

