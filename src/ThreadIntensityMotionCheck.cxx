#include "ThreadIntensityMotionCheck.h"


#include <string>
#include <math.h>
#include <QtGui>

CThreadIntensityMotionCheck::CThreadIntensityMotionCheck(QObject *parentLocal) :
QThread(parentLocal)
{
  DWINrrdFilename = "";
  XmlFilename = "";
  protocol = NULL;
  qcResult = NULL;
  m_IntensityMotionCheck = new CIntensityMotionCheck;
}

CThreadIntensityMotionCheck::~CThreadIntensityMotionCheck()
{}

void CThreadIntensityMotionCheck::run()
{
  if ( DWINrrdFilename.length() == 0 )
  {
    std::cout << "DWI file name not set!" << std::endl;
    return;
  }

  if ( !protocol )
  {
    std::cout << "protocol not set!" << std::endl;
    return;
  }

  if ( !qcResult )
  {
    std::cout << "qcResult not set!" << std::endl;
    return;
  }
  emit allDone("checking ...");

  std::cout << "Checking Thread begins here " << std::endl;
  qcResult->Clear();
  m_IntensityMotionCheck->SetDwiFileName(DWINrrdFilename);
  m_IntensityMotionCheck->SetXmlFileName(XmlFilename);
  m_IntensityMotionCheck->SetProtocol( protocol);
  m_IntensityMotionCheck->SetQCResult( qcResult);
  m_IntensityMotionCheck->GetImagesInformation();

  emit StartProgressSignal();  // start showing progress bar
  const unsigned char result = m_IntensityMotionCheck->RunPipelineByProtocol();

  unsigned char out = result;
  std::cout << "--------------------------------" << std::endl;

  out = result;
  out = out >> 7;
  if ( out )
  {
    std::cout << "QC FAILURE: too many gradients excluded!" << std::endl;
  }

  out = result;
  out = out << 1;
  out = out >> 7;
  if ( out )
  {
    std::cout << "QC FAILURE: Single b-value DWI without a b0/baseline!"
      << std::endl;
  }

  out = result;
  out = out << 2;
  out = out >> 7;
  if ( out )
  {
    std::cout << "QC FAILURE: Gradient direction # is less than 6!"
      << std::endl;
  }

  out = result;
  out = out << 7;
  out = out >> 7;
  if ( out  )
  {
    std::cout << "Image information check:\tFAILURE" << std::endl;
  }
  else
  {
    std::cout << "Image information check:\tPASS" << std::endl;
  }

  out = result;
  out = out << 6;
  out = out >> 7;
  if ( out )
  {
    std::cout << "Diffusion information check:\tFAILURE" << std::endl;
  }
  else
  {
    std::cout << "Diffusion information check:\tPASS" << std::endl;
  }

  out = result;
  out = out << 5;
  out = out >> 7;
  if ( out  )
  {
    std::cout << "Slice-wise check:\t\tFAILURE" << std::endl;
  }
  else
  {
    std::cout << "Slice-wise check:\t\tPASS" << std::endl;
  }

  out = result;
  out = out << 4;
  out = out >> 7;
  if ( out  )
  {
    std::cout << "Interlace-wise check:\t\tFAILURE" <<  std::endl;
  }
  else
  {
    std::cout << "Interlace-wise check:\t\tPASS" << std::endl;
  }

  out = result;
  out = out << 3;
  out = out >> 7;
  if ( out )
  {
    std::cout << "Gradient-wise check:\t\tFAILURE" << std::endl;
  }
  else
  {
    std::cout << "Gradient-wise check:\t\tPASS" << std::endl;
  }

  
  emit StopProgressSignal();  // hiding progress bar

  emit allDone("Checking Thread ended");
  emit ResultUpdate();
}



