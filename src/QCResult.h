#pragma once

#include <vector>
struct ImageInformationCheckResult
{
	bool space;
	bool size;
	bool origin;
	bool spacing;
	bool spacedirection;
};

struct DiffusionInformationCheckResult
{
	bool b;
	bool gradient;
	bool measurementFrame;
};

struct SliceCorrelation
{
	double pre;
	double post;
};

struct GradientIntensityMotionCheckResult
{
	std::vector< double > sliceCorrelation;
	//std::vector< SliceCorrelation > sliceCorrelation;

	double interlaceCorrelation;
	double interlaceRotationX;
	double interlaceRotationY;
	double interlaceRotationZ;
	double interlaceTranslationX;
	double interlaceTranslationY;
	double interlaceTranslationZ;

	double gradientRotationX;
	double gradientRotationY;
	double gradientRotationZ;
	double gradientTranslationX;
	double gradientTranslationY;
	double gradientTranslationZ;

	int processing;
};


class QCResult
{
public:
	QCResult(void);
	~QCResult(void);

enum {
	GRADIENT_INCLUDE = 0,
	GRADIENT_EXCLUDE,
};

	struct ImageInformationCheckResult				  &GetImageInformationCheckResult(){		return 	imageInformationCheckResult;};
	struct DiffusionInformationCheckResult			  &GetDiffusionInformationCheckResult(){	return 	diffusionInformationCheckResult;};
	std::vector<GradientIntensityMotionCheckResult>   &GetIntensityMotionCheckResult(){		return 	intensityMotionCheckResult;};
	std::vector< int >								  &GetGradientProcess(){ return GradientProcess;};

private:
	ImageInformationCheckResult		imageInformationCheckResult;
	DiffusionInformationCheckResult diffusionInformationCheckResult;
	std::vector< GradientIntensityMotionCheckResult > intensityMotionCheckResult;

	std::vector< int > GradientProcess;

};
