/*************************************************************************
 *     rMSIproc - R package for MSI data processing
 *     Copyright (C) 2014 Pere Rafols Soler
 * 
 *     This program is free software: you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation, either version 3 of the License, or
 *     (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License
 *     along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **************************************************************************/

#include <Rcpp.h>
#include <fftw3.h>
#include <cmath>
#include "peakpicking.h"
using namespace Rcpp;


PeakPicking::PeakPicking(int WinSize, double *massAxis, int numOfDataPoints, int UpSampling )
{
  FFT_Size = (int)pow(2.0, std::ceil(log2(WinSize)));
  FFTInter_Size = (int)pow(2.0, std::ceil(log2(UpSampling*FFT_Size))); //FFT interpolation buffer
  
  dataLength = numOfDataPoints;
  mass = new double[dataLength];
  memcpy(mass, massAxis, sizeof(double) * dataLength);
  
  //Compute Hanning window
  //TODO improve hanning with the triple-one aproach
  HanningWin = new double[FFT_Size];
  for( int i = 0; i < FFT_Size; i++)
  {
    HanningWin[i] = 0.5*(1.0-cos((2.0*M_PI*(double)(i + 1))/(FFT_Size)));
  }
  
  //Prepare FFT objects
  fft_in1 = fftw_alloc_real(FFT_Size);
  fft_out1 = fftw_alloc_real(FFT_Size);
  fft_in2 = fftw_alloc_real(FFTInter_Size);
  fft_out2 = fftw_alloc_real(FFTInter_Size);
  fft_pdirect = fftw_plan_r2r_1d(FFT_Size, fft_in1, fft_out1, FFTW_R2HC, FFTW_ESTIMATE);
  fft_pinvers = fftw_plan_r2r_1d(FFTInter_Size, fft_in2, fft_out2, FFTW_HC2R, FFTW_ESTIMATE);
  
  //Prepare NoiseEstimation oject
  neObj = new NoiseEstimation(dataLength);
}

PeakPicking::~PeakPicking()
{
  delete[] mass;
  delete[] HanningWin;
  fftw_destroy_plan(fft_pdirect);
  fftw_destroy_plan(fft_pinvers);
  fftw_free(fft_in1); 
  fftw_free(fft_out1);
  fftw_free(fft_in2); 
  fftw_free(fft_out2);
  delete neObj;
}

PeakPicking::Peaks *PeakPicking::peakPicking(double *spectrum, double SNR )
{
  //Calculate noise
  double noise[dataLength];
  memcpy(noise, spectrum, sizeof(double)*dataLength);
  neObj->NoiseEstimationFFTExpWin(noise, dataLength, FFT_Size*2);
  
  //Detect peaks
  return detectPeaks(spectrum, noise, SNR);
}

//Detect all local maximums and Filter peaks using SNR min value in a sliding window
PeakPicking::Peaks *PeakPicking::detectPeaks( double *spectrum, double *noise, double SNR )
{
  const int HalfWinSize = FFT_Size/2;
  double slope = 0.0;
  double slope_ant = 0.0;
  double noise_mean = 0.0;
  PeakPicking::Peaks *m_peaks = new PeakPicking::Peaks();
  for( int i=0; i < (dataLength - 1); i++)
  {
    slope = spectrum[i + 1] - spectrum[i]; //Compute 1st derivative
    //Look for a zero crossing at first derivate (negative sign of product) and negative 2nd derivate value (local maxim)
    if(slope*slope_ant <= 0.0 && (slope - slope_ant) < 0.0)
    {
      noise_mean = 0.0;
      for( int j=(i - HalfWinSize); j < (i + HalfWinSize); j++)
      {
        noise_mean += (j >= 0 && j < dataLength) ? noise[j] : 0.0;
      }
      noise_mean /= FFT_Size;
      if(spectrum[i]/noise_mean >= SNR)
      {
        m_peaks->mass.push_back( predictPeakMass(spectrum, i)); //Compute peak accurately using FFT interpolation); 
        m_peaks->intensity.push_back(spectrum[i]);
        m_peaks->SNR.push_back(spectrum[i]/noise_mean); 
        m_peaks->area.push_back(predictPeakArea(spectrum, i)); //Normalized to non-FFT sapce
      }
    }
    slope_ant = slope;
  }
  return m_peaks;
}

int PeakPicking::interpolateFFT(double *spectrum, int iPeakMass, bool ApplyHanning)
{
  //Fill fft data vector taking care of extrems
  int idata = 0; //Indeix of data
  for( int i = 0; i < FFT_Size; i++)
  {
    idata = iPeakMass - FFT_Size/2 + i;
    fft_in1[i] = (idata >= 0 && idata < dataLength) ? spectrum[idata] : 0.0;
    if(ApplyHanning)
    {
      fft_in1[i] *= HanningWin[i]; 
    }

    ///TODO l'error d'integració d'area es aki. Resulta que si no enfinestres el pic te fenomen de Gibbs i per tant la idea dels topalls falla
    //Crec que tens dues opcions:
    //  - Definir un criteri diferent per decidir l'interval d'integracio
    //  - Buscar alguna funcio de enfinestrat que et dongui un pic suau sense perdre ambplada
  }
  
  //Compute FFT
  fftw_execute(fft_pdirect);
  
  //Zero padding in fft space
  int i1 = 0;
  for( int i = 0; i < FFTInter_Size; i++)
  {
    if( i <= (FFT_Size/2) || (FFTInter_Size - i) <= ((FFT_Size + 1)/2 - 1 )  )
    {
      fft_in2[i] = fft_out1[i1];
      i1++;
    }
    else
    {
      fft_in2[i] = 0.0;  
    }
  }
  
  //Compute Inverse FFT, fft_out2 contains the interpolated peak
  fftw_execute(fft_pinvers);
  
  //Peak is around FFT_Size/2 so just look there for it
  int imax = -1;
  double dmax = -1.0;
  for( int i = (int)std::floor((((double)FFTInter_Size)/((double)FFT_Size))*(0.5*(double)FFT_Size - 1.0)); 
       i <= (int)std::ceil((((double)FFTInter_Size)/((double)FFT_Size))*(0.5*(double)FFT_Size + 1.0)); 
       i++)
  {
    if( fft_out2[i] > dmax )
    {
      imax = i;
      dmax = fft_out2[i];
    }
  }
  
  return imax;
}

double PeakPicking::predictPeakMass( double *spectrum, int iPeakMass )
{
  double pMass; 
  int iPeak = interpolateFFT(spectrum, iPeakMass, true);
  
  //Compute the original mass indexing space
  double imass = (double)iPeakMass - (0.5*(double)FFT_Size) + ((double)iPeak) * ((double)FFT_Size)/((double)FFTInter_Size);

  //Convert peak position indexes to mass values
  double peakPosL = std::floor(imass);
  double peakPosR = std::ceil(imass);
  if(peakPosL == peakPosR)
  {
    //Peak exactly on mass channel, no decimal part
    pMass =  mass[(int)imass];
  }
  else
  {
    //Calculate peak position as a compositon of both neighbours mass channels
    pMass = (peakPosR - imass)*mass[(int)peakPosL] + (imass - peakPosL)*mass[(int)peakPosR];
  }

  return pMass;
}

double PeakPicking::predictPeakArea( double *spectrum, int iPeakMass )
{
  //Compte area 
  //TODO: Els topalls estan fallant per fenomen de Gibbs
 
  int iPeak = interpolateFFT(spectrum, iPeakMass, false);
  double pArea = fft_out2[iPeak];
  
  //Integrate the left part of the peak
  for( int i = iPeak; i >= 0; i-- )
  {
    if( i < iPeak )
    {
      if( i < (iPeak - 1) )
      {
        if(fft_out2[i] >= fft_out2[i + 2] ) 
        {
          //Break loop if slope increases too much at some point
          break; //TODO disabling it for testing if the mistake is here
        }
      }
      pArea += fft_out2[i];
    }
  }
  
  //Integrate the right part of the peak
  for( int i = iPeak; i < FFTInter_Size; i++)
  {
    if( i > iPeak)
    {
      if( i > (iPeak + 1))
      {
        if(fft_out2[i] >= fft_out2[i - 2] ) 
        {
          //Break loop if slope increases too much at some point
          break; //TODO disabling it for testing if the mistake is here
        }
      }
      pArea += fft_out2[i];
    }
  }
 
  return pArea * spectrum[iPeakMass] / fft_out2[iPeak]; //Return the area de-normalizing the FFT space
}

//Returns the internaly used Hanning windows (only for test purposes)
NumericVector PeakPicking::getHannWin()
{
  NumericVector hannR(FFT_Size);
  memcpy(hannR.begin(), HanningWin, sizeof(double)*FFT_Size);
  return hannR;
}

//Convert a pointer to a Peaks structure to an R list
List PeakPicking::PeakObj2List(PeakPicking::Peaks *pks)
{
  NumericVector mass(pks->mass.size());
  NumericVector intensity(pks->intensity.size());
  NumericVector SNR(pks->SNR.size());
  NumericVector area(pks->area.size());
  for(int i = 0; i < mass.length(); i++ )
  {
    mass(i) = pks->mass[i];
    intensity(i) = pks->intensity[i];
    SNR(i) = pks->SNR[i];
    area(i) = pks->area[i];
  }
  
  return List::create( Named("mass") = mass, Named("intensity") = intensity, Named("SNR") = SNR);
}

NumericVector PeakPicking::getInterpolatedPeak(NumericVector data, int iPeak, bool ApplyHanning)
{
  double Cdata[data.length()];
  memcpy(Cdata, data.begin(), sizeof(double)*data.length());
  interpolateFFT(Cdata, iPeak, ApplyHanning);
    
  NumericVector interpData(FFTInter_Size);
  memcpy(interpData.begin(), fft_out2, sizeof(double)*FFTInter_Size);
  return(interpData);
}

////// Rcpp Exported methods //////////////////////////////////////////////////////////
//' DetectPeaks_C.
//' 
//' Detect peaks from a Rcpp::NumericVector object and returns data in a R matrix.
//' This method is only exported to be use by R function DetectPeaks which is an actual R function.
//' The returned peak positions follows C indexing style, this is starts with zero.
//' 
//' @param mass a NumericVector containing the mass axis of the spectrum.
//' @param intensity a NumericVector where peaks must be detected.
//' @param SNR Only peaks with an equal or higher SNR are retained.
//' @param WinSize The windows used to detect peaks and caculate noise.
//' @param UpSampling the oversampling used for acurate mass detection and area integration.
//' 
//' @return a NumerixMatrix of 4 rows corresponding to: mass, intensity of the peak,SNR and area.
//' 
// [[Rcpp::export]]
NumericMatrix DetectPeaks_C(NumericVector mass, NumericVector intensity, double SNR = 5, int WinSize = 20, int UpSampling = 10)
{
  if(mass.length() != intensity.length())
  {
    Rcpp::stop("Error in DetectPeaks_C() function: mass and intensity length must be equal.");
    return NumericMatrix(0,0);
  }
    
  double massC[mass.length()];
  double spectrum[mass.length()];
  
  //Copy R data to C arrays
  memcpy(massC, mass.begin(), sizeof(double)*mass.length());
  memcpy(spectrum, intensity.begin(), sizeof(double)*intensity.length());
  
  PeakPicking ppObj(WinSize, massC, mass.length(), UpSampling);
  PeakPicking::Peaks *peaks = ppObj.peakPicking(spectrum, SNR);
  
  //Convert peaks to R matrix like object
  NumericMatrix mp(4, peaks->intensity.size());
  for(int i = 0; i < peaks->mass.size(); i++)
  {
    mp(0, i) = peaks->mass[i];
    mp(1, i) = peaks->intensity[i];
    mp(2, i) = peaks->SNR[i];
    mp(3, i) = peaks->area[i];
  }
  
  delete peaks;
  rownames(mp) =  CharacterVector::create("mass", "intensity", "SNR", "area");
  return mp;
}
//' TestPeakInterpolation_C.
//' 
//' 
//' @param mass a NumericVector containing the mass axis of the spectrum.
//' @param intensity a NumericVector where peaks must be detected.
//' @param peakIndex the location of the peak to interpolate in the spectrum.  
//' @param WinSize The windows used to detect peaks and caculate noise.
//' @param UpSampling the oversampling used for acurate mass detection and area integration.
//' @param usHAnning if hanning windowing must be used befor interpolation.
//' 
//' @return a NumerixVector with the FFT interpolated peak shape.
//' 
// [[Rcpp::export]]
NumericVector TestPeakInterpolation_C(NumericVector mass, NumericVector intensity, int peakIndex, int WinSize = 20, int UpSampling = 10, bool useHanning = false)
{
  if(mass.length() != intensity.length())
  {
    Rcpp::stop("Error in DetectPeaks_C() function: mass and intensity length must be equal.");
    return NumericVector(0);
  }
  
  double massC[mass.length()];
  double spectrum[mass.length()];
  
  //Copy R data to C arrays
  memcpy(massC, mass.begin(), sizeof(double)*mass.length());
  memcpy(spectrum, intensity.begin(), sizeof(double)*intensity.length());
  
  PeakPicking ppObj(WinSize, massC, mass.length(), UpSampling);
  
  return(ppObj.getInterpolatedPeak(intensity, peakIndex, useHanning));
}

