/*
 * ForgivingFitter.cxx
 *
 *  Created on: 13 Feb 2019
 *      Author: bernhardhohlweger
 */

#include "ForgivingFitter.h"
#include "TFitResult.h"
#include "TFitResultPtr.h"
#include "TLatex.h"
#include "TStyle.h"
#include "TFile.h"
ForgivingFitter::ForgivingFitter()
    : fBackGround(nullptr),
      fContinousBackGround(nullptr),
      fSingleGaussian(nullptr),
      fDoubleGaussian(nullptr),
      fVoigt(nullptr),
      fFullFitFnct(nullptr),
      fRangesSet(false),
      fBkgRangeMin(0),
      fBkgRangeMax(0),
      fSigRangeMin(0),
      fSigRangeMax(0),
      fSignalCounts(0),
      fSignalCountsErr(0),
      fWeightA(0),
      fWeightB(0),
      fBackgroundCounts(0),
      fBackgroundCountsErr(0),
      fMeanMass(0),
      fMeanMassErr(0),
      fMeanWidth(0),
      fMeanWidthErr(0)
{
}

ForgivingFitter::~ForgivingFitter()
{
  // TODO Auto-generated destructor stub
}

float ForgivingFitter::weightedMean(float weightA, float A, float weightB,
                                    float B)
{
  return (weightA * A + weightB * B) / (weightA + weightB);
}

float ForgivingFitter::weightedMeanError(float weightA, float A, float weightB,
                                         float B, float weightAErr, float AErr,
                                         float weightBErr, float BErr)
{
  return std::sqrt(
      weightAErr * weightAErr * std::pow(((weightB * (A - B)) / ((weightA + weightB) * (weightA + weightB))), 2) + AErr * AErr * std::pow(weightA / (weightA + weightB), 2) + weightBErr * weightBErr * std::pow((weightA * (-A + B)) / ((weightA + weightB) * (weightA + weightB)), 2) + BErr * BErr * std::pow(weightB / (weightA + weightB), 2));
}

TH1F *ForgivingFitter::getSignalHisto(TF1 *function, TH1F *histo,
                                      float rangeLow, float rangeHigh,
                                      const char *name)
{
  const int firstBin = histo->FindBin(rangeLow);
  const int lastBin = histo->FindBin(rangeHigh);
  TH1F *result = new TH1F(
      Form("result_%.2f_%.2f_%s", rangeLow, rangeHigh, name), "",
      histo->GetNbinsX(), histo->GetXaxis()->GetXmin(),
      histo->GetXaxis()->GetXmax());
  for (int i = firstBin; i < lastBin; ++i)
  {
    float weight = histo->GetBinContent(i) - function->Eval(histo->GetBinCenter(i));
    result->Fill(histo->GetBinCenter(i), weight);
    result->SetBinError(i, histo->GetBinError(i));
  }
  result->SetFillColor(kGray + 1);
  result->SetLineColor(kGray + 1);
  return result;
}

double fitVoigt(double *x, double *p)
{
  double t = *x;
  return p[0] * TMath::Voigt(t - p[1], p[2], p[3]);
}

double fitVoigtBackground(double *x, double *p)
{
  double t = *x;
  return (p[0] * TMath::Voigt(t - p[1], p[2], p[3]) + (p[4] + p[5] * t + p[6] * t * t));
}

// second order polynomial + double gaus to Lambda peak
void ForgivingFitter::FitInvariantMass(TH1F *histo, float massCutMin,
                                       float massCutMax, int signalColor, int backgroundColor)
{
  // Fit Background with second order polynomial, excluding Mlambda +/- 10 MeV
  if (!fRangesSet)
  {
    std::cout
        << "no BackGround Function defined via SetBackGroundRange! Exiting \n";
    return;
  }
  histo->Fit(fBackGround, "R Q N", "", fBkgRangeMin * 1.005,
             fBkgRangeMax * 0.995);
  histo->GetXaxis()->SetRangeUser(1.07, 1.18);
  CreateContinousBackgroundFunction();
  fContinousBackGround->SetLineColor(backgroundColor);
  TH1F *signalOnly = getSignalHisto(fContinousBackGround, histo,
                                    fSigRangeMin * 0.98, fSigRangeMax * 1.02,
                                    Form("%s_signal_only", histo->GetName()));
  signalOnly->Fit(fSingleGaussian, "R Q N", "", fSigRangeMin * 1.01,
                  fSigRangeMax * 0.99);
  SetStartParsDoubleGaussian(histo);
  signalOnly->Fit(fDoubleGaussian, "R Q N", "", fSigRangeMin * 1.01,
                  fSigRangeMax * 0.99);
  delete signalOnly;
  // fSignalCounts = fDoubleGaussian->Integral(massCutMin, massCutMax)
  //     / double(histo->GetBinWidth(1));
  //Add weights
  fSingleGaussian->SetParameters(fDoubleGaussian->GetParameter(0),
                                 fDoubleGaussian->GetParameter(1),
                                 fDoubleGaussian->GetParameter(2));
  fWeightA = fSingleGaussian->Integral(fSigRangeMin, fSigRangeMax) / double(histo->GetBinWidth(1));
  fSingleGaussian->SetParameters(fDoubleGaussian->GetParameter(3),
                                 fDoubleGaussian->GetParameter(4),
                                 fDoubleGaussian->GetParameter(5));
  fWeightB = fSingleGaussian->Integral(fSigRangeMin, fSigRangeMax) / double(histo->GetBinWidth(1));
  CreateFullFitFunction(histo);
  fFullFitFnct->SetLineColor(signalColor);

  TFitResultPtr fullFit = histo->Fit("fLambda", "SRQ", "", fBkgRangeMin * 1.01, fBkgRangeMax * 0.99);
  histo->GetListOfFunctions()->Add(
      fFullFitFnct->Clone(Form("fnc%s", histo->GetName())));

  TF1 *DoubleGaussian = new TF1("SignalAfterFit", "gaus(0) + gaus(3)", fBkgRangeMin * 0.5,
                                fBkgRangeMax * 2);

  DoubleGaussian->SetParameter(0, fFullFitFnct->GetParameter(3));
  DoubleGaussian->SetParameter(1, fFullFitFnct->GetParameter(4));
  DoubleGaussian->SetParameter(2, fFullFitFnct->GetParameter(5));
  DoubleGaussian->SetParameter(3, fFullFitFnct->GetParameter(6));
  DoubleGaussian->SetParameter(4, fFullFitFnct->GetParameter(7));
  DoubleGaussian->SetParameter(5, fFullFitFnct->GetParameter(8));
  fSignalCounts = DoubleGaussian->Integral(massCutMin, massCutMax) / double(histo->GetBinWidth(1));
  fSignalCountsErr = TMath::Sqrt(fSignalCounts);
  //   DoubleGaussian->IntegralError(
  // 				   massCutMin, massCutMax, fullFit->GetParams(),
  // 				   fullFit->GetCovarianceMatrix().GetMatrixArray())
  //  / double(histo->GetBinWidth(1));
  std::cout << "Background Error: \n";
  CalculateBackgorund(histo, massCutMin, massCutMax, backgroundColor, fullFit);
  fMeanMass = weightedMean(fWeightA, fFullFitFnct->GetParameter(4), fWeightB,
                           fFullFitFnct->GetParameter(7));
  fMeanWidth = weightedMean(fWeightA, fFullFitFnct->GetParameter(5), fWeightB,
                            fFullFitFnct->GetParameter(8));
  delete DoubleGaussian;
}

void ForgivingFitter::FitInvariantMassSigma(TH1F *histo, float massCuts, int signalColor, int backgroundColor)
{
  // Fit Background with third order polynomial
  if (fBackGround)
  {
    delete fBackGround;
  }
  fBackGround = new TF1(
      "fBackGround", [&](double *x, double *p)
      {
        if (x[0] > fSigRangeMin && x[0] < fSigRangeMax)
        {
          TF1::RejectPoint();
          return (double)0;
        }
        return p[0] + p[1] * x[0] + p[2] * x[0] * x[0] +
               p[3] * x[0] * x[0] * x[0];
      },
      fBkgRangeMin * 0.5, fBkgRangeMax * 2, 4);
  histo->Fit(fBackGround, "SRQ", "", fBkgRangeMin * 0.995, fBkgRangeMax);

  // parse then to proper TF1
  if (fContinousBackGround)
  {
    delete fContinousBackGround;
  }
  fContinousBackGround = new TF1("fBackground2", "pol3", fBkgRangeMin * 0.5,
                                 fBkgRangeMax * 2);
  fContinousBackGround->SetParameter(0, fBackGround->GetParameter(0));
  fContinousBackGround->SetParameter(1, fBackGround->GetParameter(1));
  fContinousBackGround->SetParameter(2, fBackGround->GetParameter(2));
  fContinousBackGround->SetParameter(3, fBackGround->GetParameter(3));
  fContinousBackGround->SetLineStyle(2);
  fContinousBackGround->SetLineColor(backgroundColor);
  TH1F *signalOnly = getSignalHisto(fContinousBackGround, histo,
                                    fBkgRangeMin * 0.8, fBkgRangeMax * 1.2,
                                    Form("%s_signal_only", histo->GetName()));
  if (fSingleGaussian)
  {
    delete fSingleGaussian;
  }
  fSingleGaussian = new TF1("fSignalSingleGauss", "gaus(0)", fBkgRangeMin,
                            fBkgRangeMax);
  fSingleGaussian->FixParameter(1, 1.1929);
  fSingleGaussian->SetParameter(2, 0.0015);
  signalOnly->Fit(fSingleGaussian, "RQN", "", fBkgRangeMin, fBkgRangeMax);
  fSingleGaussian->ReleaseParameter(1);
  signalOnly->Fit(fSingleGaussian, "RQN", "", fBkgRangeMin, fBkgRangeMax);
  delete signalOnly;

  if (fFullFitFnct)
  {
    delete fFullFitFnct;
  }
  fFullFitFnct = new TF1("fFullFitFnct", "fBackground2 + fSignalSingleGauss",
                         fBkgRangeMin * 0.5, fBkgRangeMax * 2);
  fFullFitFnct->SetLineColor(signalColor);
  fFullFitFnct->SetNpx(1000);
  fFullFitFnct->FixParameter(0, fBackGround->GetParameter(0));
  fFullFitFnct->FixParameter(1, fBackGround->GetParameter(1));
  fFullFitFnct->FixParameter(2, fBackGround->GetParameter(2));
  fFullFitFnct->FixParameter(3, fBackGround->GetParameter(3));
  histo->Fit("fFullFitFnct", "RQ", "", fBkgRangeMin, fBkgRangeMax);
  fFullFitFnct->ReleaseParameter(0);
  fFullFitFnct->ReleaseParameter(1);
  fFullFitFnct->ReleaseParameter(2);
  fFullFitFnct->ReleaseParameter(3);
  histo->Fit("fFullFitFnct", "RQ", "", fBkgRangeMin, fBkgRangeMax);
  fFullFitFnct->ReleaseParameter(5);
  fFullFitFnct->ReleaseParameter(6);
  TFitResultPtr fullFit = histo->Fit("fFullFitFnct", "SRQ", "", fBkgRangeMin,
                                     fBkgRangeMax);

  fMeanMass = fFullFitFnct->GetParameter(5);
  fMeanMassErr = fFullFitFnct->GetParError(5);
  fMeanWidth = fFullFitFnct->GetParameter(6);
  fMeanWidthErr = fFullFitFnct->GetParError(6);

  const double rangeMin = fMeanMass - massCuts;
  const double rangeMax = fMeanMass + massCuts;

  // Get refitted Background function
  fContinousBackGround->SetParameter(0, fFullFitFnct->GetParameter(0));
  fContinousBackGround->SetParameter(1, fFullFitFnct->GetParameter(1));
  fContinousBackGround->SetParameter(2, fFullFitFnct->GetParameter(2));
  fContinousBackGround->SetParameter(3, fFullFitFnct->GetParameter(3));

  auto signal = new TF1("fSignal", "gaus(0)", fBkgRangeMin * 0.5,
                        fBkgRangeMax * 2);
  signal->SetParameter(0, fFullFitFnct->GetParameter(4));
  signal->SetParameter(1, fFullFitFnct->GetParameter(5));
  signal->SetParameter(2, fFullFitFnct->GetParameter(6));

  fSignalCounts = signal->Integral(rangeMin, rangeMax) / double(histo->GetBinWidth(1));

  fBackgroundCounts = fContinousBackGround->Integral(rangeMin, rangeMax) / double(histo->GetBinWidth(1));

  fSignalCountsErr = signal->IntegralError(
                         rangeMin, rangeMax, fullFit->GetParams(),
                         fullFit->GetCovarianceMatrix().GetMatrixArray()) /
                     double(histo->GetBinWidth(1));

  fBackgroundCountsErr = fContinousBackGround->IntegralError(
                             rangeMin, rangeMax, fullFit->GetParams(),
                             fullFit->GetCovarianceMatrix().GetMatrixArray()) /
                         double(histo->GetBinWidth(1));
}

// For the Phi Meson
// BCKG second order polynomial + Gaus x Rel BW for peak signal
void ForgivingFitter::FitInvariantMassPhi(TH1F *histo, float massCuts,
                                          int signalColor, int backgroundColor, int signalOnlyColor)
{
  if (!fRangesSet)
  {
    std::cout
        << "no BackGround Function defined via SetBackGroundRange! Exiting \n";
    return;
  }
  histo->Fit(fBackGround, "R Q N", "", fBkgRangeMin * 0.995,
             fBkgRangeMax * 0.995);
  histo->GetXaxis()->SetRangeUser(fBkgRangeMin * 0.8, fBkgRangeMax * 1.2);
  CreateContinousBackgroundFunction();
  fContinousBackGround->SetLineColor(backgroundColor);
  TH1F *signalOnly = getSignalHisto(fContinousBackGround, histo,
                                    fSigRangeMin * 0.5, fSigRangeMax * 1.8,
                                    Form("%s_signal_only", histo->GetName()));
  double MPhi = 1.019461;
  double sigmaPhi = 0.003;
  double decwidthPhi = 0.00425;
  SetStartParsVoigt(histo, MPhi, sigmaPhi, decwidthPhi);
  signalOnly->Fit(fVoigt, "R Q N", "", fSigRangeMin * 1.01,
                  fSigRangeMax * 0.99);
  delete signalOnly;
  CreateFullFitFunctionVoigt(histo, fBackGround);
  fFullFitFnct->SetLineColor(signalColor);
  TFitResultPtr fullFit = histo->Fit("fPhi", "SRQ", "", fBkgRangeMin * 1.01, fBkgRangeMax * 0.99);
  histo->GetListOfFunctions()->Add(
      fFullFitFnct->Clone(Form("fnc%s", histo->GetName())));

  TF1 *VoigtAfterFit = new TF1("SignalAfterFit", fitVoigt, fBkgRangeMin * 0.5,
                               fBkgRangeMax * 2, 4);

  VoigtAfterFit->FixParameter(0, fFullFitFnct->GetParameter(0));
  VoigtAfterFit->FixParameter(1, fFullFitFnct->GetParameter(1));
  VoigtAfterFit->FixParameter(2, fFullFitFnct->GetParameter(2));
  VoigtAfterFit->FixParameter(3, fFullFitFnct->GetParameter(3));
  CalculateSignalVoigt(histo, signalOnlyColor);
  float massCutMin = MPhi - massCuts;
  float massCutMax = MPhi + massCuts;
  fSignalCounts = VoigtAfterFit->Integral(massCutMin, massCutMax) / double(histo->GetBinWidth(1));
  fSignalCountsErr = TMath::Sqrt(fSignalCounts);
  std::cout << "Background Error: \n";
  CalculateBackgorundVoigt(histo, massCutMin, massCutMax, backgroundColor, fullFit);
  fMeanMass = fFullFitFnct->GetParameter(1);
  fMeanMassErr = fFullFitFnct->GetParError(1);
  fMeanWidth = fFullFitFnct->GetParameter(2);
  fMeanWidthErr = fFullFitFnct->GetParError(2);
  delete VoigtAfterFit;
}

void ForgivingFitter::SetRangesSigma(float SigMin, float SigMax,
                                     float BkgRangeMin, float BkgRangeMax)
{
  fBkgRangeMin = BkgRangeMin;
  fBkgRangeMax = BkgRangeMax;
  fSigRangeMin = SigMin;
  fSigRangeMax = SigMax;
  fRangesSet = true;
}

void ForgivingFitter::SetRangesPhi(float SigMin, float SigMax,
                                   float BkgRangeMin, float BkgRangeMax)
{
  fBkgRangeMin = BkgRangeMin;
  fBkgRangeMax = BkgRangeMax;
  fSigRangeMin = SigMin;
  fSigRangeMax = SigMax;
  fRangesSet = true;
}

void ForgivingFitter::SetRanges(float SigMin, float SigMax, float BkgRangeMin,
                                float BkgRangeMax)
{
  fBkgRangeMin = BkgRangeMin;
  fBkgRangeMax = BkgRangeMax;
  fSigRangeMin = SigMin;
  fSigRangeMax = SigMax;
  CreateBackgroundFunction();
  CreateSignalFunctions();
  fRangesSet = true;
}

void ForgivingFitter::CreateBackgroundFunction()
{
  if (fBackGround)
  {
    delete fBackGround;
  }
  fBackGround =
      new TF1(
          "fBackground",
          [&](double *x, double *p)
          {
            if (x[0] > fSigRangeMin && x[0] < fSigRangeMax)
            {
              TF1::RejectPoint();
              return (double)0;
            }
            return p[0] + p[1] * x[0] + p[2] * x[0] * x[0];
          },
          fBkgRangeMin, fBkgRangeMax, 3);
}

void ForgivingFitter::CreateContinousBackgroundFunction()
{
  if (fContinousBackGround)
  {
    delete fContinousBackGround;
  }
  fContinousBackGround = new TF1("fBackground2", "pol2", fBkgRangeMin,
                                 fBkgRangeMax);
  fContinousBackGround->SetParameter(0, fBackGround->GetParameter(0));
  fContinousBackGround->SetParameter(1, fBackGround->GetParameter(1));
  fContinousBackGround->SetParameter(2, fBackGround->GetParameter(2));
}

void ForgivingFitter::CreateSignalFunctions()
{
  fSingleGaussian = new TF1("fSignalSingleGauss", "gaus(0)", fSigRangeMin,
                            fSigRangeMax);
  fDoubleGaussian = new TF1("fSignalDoubleGauss", "gaus(0) + gaus(3)",
                            fSigRangeMin, fSigRangeMax);
  fVoigt = new TF1("fSignalVoigt", fitVoigt, fSigRangeMin, fSigRangeMax, 4);
}

void ForgivingFitter::SetStartParsDoubleGaussian(TH1F *targetHisto)
{
  //Amplitude 1
  fDoubleGaussian->SetParameter(0, 0.75 * targetHisto->GetMaximum());
  //Mean 1
  fDoubleGaussian->SetParameter(1, fSingleGaussian->GetParameter(1));
  fDoubleGaussian->SetParLimits(
      1, fSingleGaussian->GetParameter(1) - fSingleGaussian->GetParameter(2),
      fSingleGaussian->GetParameter(1) + fSingleGaussian->GetParameter(2));
  //Width 1
  fDoubleGaussian->SetParameter(2, 2.f * fSingleGaussian->GetParameter(2));
  fDoubleGaussian->SetParLimits(2, 0.5 * fSingleGaussian->GetParameter(2),
                                1e2 * fSingleGaussian->GetParameter(2));
  //Amplitude 2
  fDoubleGaussian->SetParameter(3, 0.2 * targetHisto->GetMaximum());
  //Mean 2
  fDoubleGaussian->SetParameter(4, fSingleGaussian->GetParameter(1));
  fDoubleGaussian->SetParLimits(
      4, fSingleGaussian->GetParameter(1) - fSingleGaussian->GetParameter(2),
      fSingleGaussian->GetParameter(1) + fSingleGaussian->GetParameter(2));
  //Width 2
  fDoubleGaussian->SetParameter(5, 0.5 * fSingleGaussian->GetParameter(2));
  fDoubleGaussian->SetParLimits(5, 0.5 * fSingleGaussian->GetParameter(2),
                                1e2 * fSingleGaussian->GetParameter(2));
}

void ForgivingFitter::SetStartParsVoigt(TH1F *targetHisto, double mass, double sigma, double decwidth)
{
  //Normalization
  fVoigt->SetParameter(0, 0.8 * targetHisto->GetMaximum());
  //Mass
  fVoigt->SetParameter(1, mass);
  fVoigt->SetParLimits(1, 1.0, 1.04);
  //sigma
  fVoigt->SetParameter(2, sigma);
  fVoigt->SetParLimits(2, 0.0005, 0.0019);
  //decay width
  fVoigt->FixParameter(3, decwidth);
}

void ForgivingFitter::CreateFullFitFunctionVoigt(TH1F *targetHisto, TF1 *funback)
{
  if (fFullFitFnct)
  {
    delete fFullFitFnct;
  }
  fFullFitFnct = new TF1("fPhi", fitVoigtBackground,
                         fBkgRangeMin, fBkgRangeMax, 7);
  fFullFitFnct->SetNpx(1000);
  //Background model 4 - 6
  fFullFitFnct->SetParameter(4, funback->GetParameter(0));
  fFullFitFnct->SetParameter(5, funback->GetParameter(1));
  fFullFitFnct->SetParameter(6, funback->GetParameter(2));
  //Normalization
  fFullFitFnct->SetParameter(0, 0.8 * targetHisto->GetMaximum());
  //Mass
  fFullFitFnct->SetParameter(1, fVoigt->GetParameter(1));
  fFullFitFnct->SetParLimits(1, 1.0, 1.04);
  //sigma
  fFullFitFnct->SetParameter(2, fVoigt->GetParameter(2));
  fFullFitFnct->SetParLimits(2, 0.0005, 0.0019);
  //Decay width
  fFullFitFnct->FixParameter(3, fVoigt->GetParameter(3));

  fFullFitFnct->SetLineColor(kBlue);
  fFullFitFnct->SetLineWidth(2);
  fFullFitFnct->SetParNames("Normalization", "Mass", "Sigma", "Decay Width", "pol0",
                            "pol1", "pol2");
}

void ForgivingFitter::CreateFullFitFunction(TH1F *targetHisto)
{
  if (fFullFitFnct)
  {
    delete fFullFitFnct;
  }
  fFullFitFnct = new TF1("fLambda", "fBackground2 + fSignalDoubleGauss",
                         fBkgRangeMin, fBkgRangeMax);
  fFullFitFnct->SetNpx(1000);
  //Background model 0 - 2
  fFullFitFnct->SetParameter(0, fBackGround->GetParameter(0));
  fFullFitFnct->SetParameter(1, fBackGround->GetParameter(1));
  fFullFitFnct->SetParameter(2, fBackGround->GetParameter(2));
  //Amplitude 1
  fFullFitFnct->SetParameter(3, 0.75 * targetHisto->GetMaximum());
  //Mean 1
  fFullFitFnct->SetParameter(4, fDoubleGaussian->GetParameter((1)));
  fFullFitFnct->SetParLimits(
      4, fDoubleGaussian->GetParameter(1) - fDoubleGaussian->GetParameter(2),
      fDoubleGaussian->GetParameter(1) + fDoubleGaussian->GetParameter(2));
  //Width 1
  fFullFitFnct->SetParameter(5, fDoubleGaussian->GetParameter((2)));
  fFullFitFnct->SetParLimits(5, 0.5 * fDoubleGaussian->GetParameter(2),
                             1e2 * fDoubleGaussian->GetParameter(2));
  //Amplitude 2
  fFullFitFnct->SetParameter(6, 0.2 * targetHisto->GetMaximum());
  //Mean 2
  fFullFitFnct->SetParameter(7, fDoubleGaussian->GetParameter((4)));
  fFullFitFnct->SetParLimits(
      7, fDoubleGaussian->GetParameter(4) - fDoubleGaussian->GetParameter(5),
      fDoubleGaussian->GetParameter(4) + fDoubleGaussian->GetParameter(5));
  //Width 2
  fFullFitFnct->SetParameter(8, fDoubleGaussian->GetParameter((5)));
  fFullFitFnct->SetParLimits(8, 0.5 * fDoubleGaussian->GetParameter(5),
                             1e2 * fDoubleGaussian->GetParameter(5));
  fFullFitFnct->SetLineColor(kBlue);
  fFullFitFnct->SetLineWidth(2);
  fFullFitFnct->SetParNames("pol0", "pol1", "pol2", "AmpGausOne", "MeanGausOne",
                            "WidthGausOne", "AmpGausTwo", "MeanGausTwo",
                            "WidthGausTwo");
}

void ForgivingFitter::CalculateBackgorund(TH1F *targetHisto, float massCutMin,
                                          float massCutMax, int backgroundColor, TFitResultPtr ptr)
{
  TF1 *fLambda_background = new TF1("fLambda_background", "pol2(0)",
                                    0.5 * fBkgRangeMin, 2 * fBkgRangeMax);
  fLambda_background->SetParameter(0, fFullFitFnct->GetParameter(0));
  fLambda_background->SetParameter(1, fFullFitFnct->GetParameter(1));
  fLambda_background->SetParameter(2, fFullFitFnct->GetParameter(2));
  fLambda_background->SetLineStyle(3);
  fLambda_background->SetLineColor(backgroundColor);

  fBackgroundCounts = fLambda_background->Integral(massCutMin, massCutMax) / double(targetHisto->GetBinWidth(1));

  fBackgroundCountsErr = TMath::Sqrt(fBackgroundCounts);

  // fLambda_background->IntegralError(
  //   massCutMin, massCutMax, ptr->GetParams(),
  //   ptr->GetCovarianceMatrix().GetMatrixArray())
  //   / double(targetHisto->GetBinWidth(1));

  targetHisto->GetListOfFunctions()->Add(fLambda_background);
}

void ForgivingFitter::CalculateBackgorundVoigt(TH1F *targetHisto, float massCutMin,
                                               float massCutMax, int backgroundColor, TFitResultPtr ptr)
{
  TF1 *fVoigt_background = new TF1("fVoigt_background", "pol2(0)",
                                   0.5 * fBkgRangeMin, 2 * fBkgRangeMax);
  fVoigt_background->SetParameter(0, fFullFitFnct->GetParameter(4));
  fVoigt_background->SetParameter(1, fFullFitFnct->GetParameter(5));
  fVoigt_background->SetParameter(2, fFullFitFnct->GetParameter(6));
  fVoigt_background->SetLineStyle(3);
  fVoigt_background->SetLineColor(backgroundColor);

  fBackgroundCounts = fVoigt_background->Integral(massCutMin, massCutMax) / double(targetHisto->GetBinWidth(1));

  fBackgroundCountsErr = TMath::Sqrt(fBackgroundCounts);

  // fLambda_background->IntegralError(
  //   massCutMin, massCutMax, ptr->GetParams(),
  //   ptr->GetCovarianceMatrix().GetMatrixArray())
  //   / double(targetHisto->GetBinWidth(1));

  targetHisto->GetListOfFunctions()->Add(fVoigt_background);
}

void ForgivingFitter::CalculateSignalVoigt(TH1F *targetHisto, int backgroundColor)
{
  TF1 *fVoigt_signal = new TF1("fVoigt_signal", fitVoigt,
                               0.5 * fBkgRangeMin, 2 * fBkgRangeMax, 4);
  fVoigt_signal->SetParameter(0, fFullFitFnct->GetParameter(0));
  fVoigt_signal->SetParameter(1, fFullFitFnct->GetParameter(1));
  fVoigt_signal->SetParameter(2, fFullFitFnct->GetParameter(2));
  fVoigt_signal->SetParameter(3, fFullFitFnct->GetParameter(3));

  fVoigt_signal->SetLineStyle(1);
  fVoigt_signal->SetLineColor(backgroundColor);
  targetHisto->GetListOfFunctions()->Add(fVoigt_signal);
}

void ForgivingFitter::ShittyInvariantMass(TH1F *histo, TPad *c1, float pTMin,
                                          float pTMax, const char *part)
{
  float lowerMass = 1.116 - 0.006;
  float upperMass = 1.116 + 0.006;
  histo->GetXaxis()->SetRangeUser(1.107, 1.125);
  histo->GetYaxis()->SetRangeUser(0, histo->GetMaximum() * 2.5);
  histo->GetXaxis()->SetTitle("#it{M}_{p#pi} (GeV/#it{c}^{2})");
  histo->GetXaxis()->SetNdivisions(310);
  //histo->GetXaxis()->SetMaxDigits(1);
  histo->GetYaxis()->SetNdivisions(310);
  //histo->GetYaxis()->SetMaxDigits(2);
  histo->DrawCopy();
  TH1F *hfp = (TH1F *)histo->Clone("hfp");
  TH1F *hfg = (TH1F *)histo->Clone("hfg");

  TF1 *f1 = new TF1("f1", "pol1(0)+gaus(2)+gaus(5)", 1.108, 1.125);
  f1->SetParameters(150000, 0, 2000000, 1.116, 0.0015, 1000000, 1.116, 0.0025);
  f1->SetLineColor(kBlue);
  f1->Draw("same");
  f1->SetParLimits(2, 10000, 9000000);
  f1->SetParLimits(5, 10000, 9000000);
  f1->SetParLimits(3, 1.115, 1.117);
  f1->SetParLimits(6, 1.115, 1.117);
  f1->SetParLimits(4, 0.001, 0.003);
  f1->SetParLimits(7, 0.001, 0.003);
  histo->Fit(f1, "MRQ", "", 1.108, 1.1234);
  //calculate lambda res:

  TF1 *signal = new TF1("signul", "gaus(0)+gaus(3)", 1.108, 1.1234);
  signal->SetParameter(0, f1->GetParameter(2));
  signal->SetParameter(1, f1->GetParameter(3));
  signal->SetParameter(2, f1->GetParameter(4));
  signal->SetParameter(3, f1->GetParameter(5));
  signal->SetParameter(4, f1->GetParameter(6));
  signal->SetParameter(5, f1->GetParameter(7));

  TF1 *signalOne = new TF1("signulOne", "gaus(0)+gaus(3)", 1.108, 1.1234);
  signalOne->SetParameter(0, f1->GetParameter(2));
  signalOne->SetParameter(1, f1->GetParameter(3));
  signalOne->SetParameter(2, f1->GetParameter(4));
  TF1 *signalTwo = new TF1("signulTwo", "gaus(0)+gaus(3)", 1.108, 1.1234);
  signalTwo->SetParameter(0, f1->GetParameter(5));
  signalTwo->SetParameter(1, f1->GetParameter(6));
  signalTwo->SetParameter(2, f1->GetParameter(7));

  TF1 *background = new TF1("buckground", "pol1", 1.108, 1.1234);
  background->SetParameter(0, f1->GetParameter(0));
  background->SetParameter(1, f1->GetParameter(1));
  background->SetLineColor(kBlue);
  background->SetLineStyle(3);
  background->Draw("SAME");
  Double_t N1 = signalOne->Integral(lowerMass, upperMass);
  Double_t N2 = signalTwo->Integral(lowerMass, upperMass);
  float signalLambda = signal->Integral(lowerMass, upperMass) / (float)histo->GetBinWidth(1);
  float backgroundLambda = background->Integral(lowerMass, upperMass) / (float)histo->GetBinWidth(1);
  for (int i = 1; i <= histo->GetNbinsX(); i++)
  {
    hfp->SetBinContent(i, signal->Eval(histo->GetBinCenter(i)));
    hfg->SetBinContent(i, background->Eval(histo->GetBinCenter(i)));
  }
  Float_t all = histo->Integral(histo->GetXaxis()->FindBin(lowerMass),
                                histo->GetXaxis()->FindBin(upperMass));
  Float_t signalHist = hfp->Integral(hfp->GetXaxis()->FindBin(lowerMass),
                                     hfp->GetXaxis()->FindBin(upperMass));
  Float_t backgroundHist = hfg->Integral(hfg->GetXaxis()->FindBin(lowerMass),
                                         hfg->GetXaxis()->FindBin(upperMass));
  Float_t purity = signalHist / (signalHist + backgroundHist);

  float meanMass = weightedMean(N1, f1->GetParameter(3), N2,
                                f1->GetParameter(6));
  float meanWidthActual = weightedMean(N1, f1->GetParameter(4), N2,
                                       f1->GetParameter(7));
  TLatex Label;
  Label.SetNDC(kTRUE);
  Label.SetTextSize(gStyle->GetTextSize() * 1.25);
  Label.DrawLatex(
      gPad->GetUxmax() - 0.80,
      gPad->GetUymax() - 0.55,
      Form("#splitline{#splitline{#splitline{#splitline{#splitline"
           "{%.2f < p_{T} < %.2f (GeV/#it{c})}"
           "{%s: %.0f}}"
           "{< #it{M} > = %.1f MeV/#it{c}^{2}}}"
           "{#sigma= %.1f MeV/#it{c}^{2}}}"
           "{Purity = %.1f %%}}"
           "{S/B = %.1f}",
           pTMin, pTMax, part, signalLambda, meanMass * 1000.f,
           meanWidthActual * 1000.f,
           signalLambda / (signalLambda + backgroundLambda) * 100.f,
           signalLambda / backgroundLambda));
  delete f1;
  delete signal;
  delete signalOne;
  delete signalTwo;
  delete background;
}
