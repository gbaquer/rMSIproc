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
#include "peakbinning.h"
using namespace Rcpp;

PeakBinning::PeakBinning(PeakPicking::Peaks **peakList, int pixelCount, double binTolerance, bool toleranceInppm, double binFilter) :
  numOfPixels(pixelCount),
  tolerance(binTolerance),
  binSizeInppm(toleranceInppm),
  binFilter(binFilter)
{
  mPeaks = peakList;
}

PeakBinning::~PeakBinning()
{
  
}

void PeakBinning::AppendPeaksAsMatrix(List peaksLst)
{
  //Create a local Peaks to handle all peaks
  int localNumPixels = numOfPixels + (as<NumericMatrix>(peaksLst["intensity"])).rows();
  PeakPicking::Peaks **localPeaks =  new PeakPicking::Peaks*[localNumPixels];
  
  //Copy the current Peaks to the local Peaks object 
  for( int i = 0; i < numOfPixels; i++)
  {
    localPeaks[i] = mPeaks[i];
  }
  
  delete[] mPeaks; //Delete the old references array, but keeps data pointers in localPeaks
  mPeaks = localPeaks; //Move to localPeaks pointer and set it as current peaks pointer
  
  //Append new peaks 
  for( int i = 0; i < (localNumPixels - numOfPixels); i++)
  {
    mPeaks[ i + numOfPixels] = new PeakPicking::Peaks();
    
    for(int j = 0; j < (as<NumericVector>(peaksLst["mass"])).length(); j++)
    {
      if( (as<NumericMatrix>(peaksLst["intensity"]))(i, j) > 0  )
      {
        mPeaks[ i + numOfPixels]->mass.push_back( (as<NumericVector>(peaksLst["mass"]))(j) ); 
        mPeaks[ i + numOfPixels]->intensity.push_back( (as<NumericMatrix>(peaksLst["intensity"]))(i, j) );
        mPeaks[ i + numOfPixels]->SNR.push_back( (as<NumericMatrix>(peaksLst["SNR"]))(i, j)  );   
        mPeaks[ i + numOfPixels]->area.push_back( (as<NumericMatrix>(peaksLst["area"]))(i, j)  );
        mPeaks[ i + numOfPixels]->binSize.push_back( 0.0 ); //Dummy bin size since merging is only possible in ppm
      }
    }
  }
  numOfPixels = localNumPixels;
}

List PeakBinning::BinPeaks()
{
  //Compute each peak obj TIC, I'll start binning from the highest spectrum which is assumed to have a lower mass error (at least for higher peaks)
  double Tic[numOfPixels];
  for(int i = 0; i < numOfPixels; i++)
  {
    Tic[i] = 0.0;
    for( unsigned int j = 0; j < mPeaks[i]->intensity.size(); j++)
    {
      Tic[i] += mPeaks[i]->intensity[j];
    }
  }
  
  //Apply binning starting with most intens spectra
  double dMax;
  int iMax;
  std::vector<std::vector<TBin> > binMat; //A matrix containing binning values
  NumericVector binMass; //The mass name for each matrix column
  NumericVector binSizeVec; //The raw spectra bin size for each matrix column
  NumericVector binAvgInt; //The average spectra intensity of each bin
  Rcout<<"Binning...\n";
  int iCount = 0;
  
  double alpha = 0; //Use to calculate bin mass related to peak intensity
  while(true)
  {
    Rcout<<iCount<<"/"<<numOfPixels<<"\n";
    iCount++;
    //Locate current most intens spectrum
    iMax = -1;
    dMax = -1.0;
    for(int i = 0; i < numOfPixels; i++)
    {
      if(Tic[i] > dMax && Tic[i] >= 0)
      {
        dMax = Tic[i];
        iMax = i;
      }
    }
    
    //End condition
    if(iMax == -1)
    {
      break;
    }
    
    Tic[iMax] = -1.0; //Mark current spectrum as processed by deleting its TIC
    
    while( mPeaks[iMax]->mass.size() > 0 )
    {
      binMass.push_back(mPeaks[iMax]->mass[0]); //Append element to the mass vector (names of bin Matrix)
      binSizeVec.push_back(mPeaks[iMax]->binSize[0]); //Append element to the binSize vector (names of bin Matrix)
      binAvgInt.push_back(mPeaks[iMax]->intensity[0]); //Append element to intensity bin
      binMat.resize(binMat.size() + 1); //Append new column
      binMat[binMat.size() - 1].resize(numOfPixels); //Extenend all new column elements
      binMat[binMat.size() - 1][iMax].intensity = mPeaks[iMax]->intensity[0]; 
      binMat[binMat.size() - 1][iMax].SNR = mPeaks[iMax]->SNR[0];
      binMat[binMat.size() - 1][iMax].area = mPeaks[iMax]->area[0];
      
      //Delete current peak
      mPeaks[iMax]->mass.erase(mPeaks[iMax]->mass.begin());
      mPeaks[iMax]->intensity.erase(mPeaks[iMax]->intensity.begin());
      mPeaks[iMax]->SNR.erase(mPeaks[iMax]->SNR.begin());
      mPeaks[iMax]->area.erase(mPeaks[iMax]->area.begin());
      mPeaks[iMax]->binSize.erase(mPeaks[iMax]->binSize.begin());
      
      double numMasses = 1.0; //number of peak masses used to compute each mass centroid
      int countPeaks = 1; //Number of peaks in current column
      
      for( int j = 0; j < numOfPixels; j++)
      {
        if(j != iMax) //Do not process current Peaks obj
        {
          //Find the nearest peak, the nearest peak postion is reatined in iPos var
          double dist = 0.0;
          double dist_ppm;
          double dist_ant;
          double dist_ant_ppm = 1e50;
          int iPos = 0;
          for( unsigned int imass = 0; imass <  mPeaks[j]->mass.size(); imass++)
          {
            iPos = imass;
            dist = binMass[binMass.size() - 1] - mPeaks[j]->mass[imass];
            dist_ppm = 1e6*dist/binMass[binMass.size() - 1]; //Translation to ppm
            if(dist_ppm <= 0)
            {
              if(std::abs(dist_ant_ppm) < std::abs(dist_ppm))
              {
                iPos = imass - 1;
                dist_ppm = dist_ant_ppm;
                dist = dist_ant;
              }
              break; //avoid all the loop
            }
            dist_ant_ppm = dist_ppm;
            dist_ant = dist;
          }
          
          //Select the kind of binning tolerance
          double compTolerance;
          if(binSizeInppm)
          {
            dist = dist_ppm;
            compTolerance = tolerance;
          }
          else
          {
            compTolerance = tolerance * binSizeVec[binSizeVec.size() - 1];
          } 
          
          //Check if is in the same mass bin and fill the matrix value accordingly
          if(std::abs(dist) <= compTolerance && mPeaks[j]->mass.size() > 0)
          {
            binMat[binMat.size() - 1][j].intensity = mPeaks[j]->intensity[iPos];
            binMat[binMat.size() - 1][j].SNR = mPeaks[j]->SNR[iPos];
            binMat[binMat.size() - 1][j].area = mPeaks[j]->area[iPos];
            countPeaks++;
         
            //Recompute mass centroid using a continuous average
            alpha =  binAvgInt[binAvgInt.size() - 1] / ( binAvgInt[binAvgInt.size() - 1] + mPeaks[j]->intensity[iPos]);
            binMass[binMass.size() - 1] = alpha * binMass[binMass.size() - 1] +  (1.0 - alpha) * mPeaks[j]->mass[iPos];
            binSizeVec[binSizeVec.size() - 1] *= numMasses;
            binAvgInt[binAvgInt.size() - 1] *= numMasses;
            binSizeVec[binSizeVec.size() - 1] +=  mPeaks[j]->binSize[iPos];
            binAvgInt[binAvgInt.size() - 1] += mPeaks[j]->intensity[iPos];
            numMasses++;
            binSizeVec[binSizeVec.size() - 1] /= numMasses;
            binAvgInt[binAvgInt.size() - 1] /= numMasses;
            
            //Delete datapoint from current peaks
            mPeaks[j]->mass.erase(mPeaks[j]->mass.begin() + iPos);
            mPeaks[j]->intensity.erase(mPeaks[j]->intensity.begin() + iPos);
            mPeaks[j]->SNR.erase(mPeaks[j]->SNR.begin() + iPos);
            mPeaks[j]->area.erase(mPeaks[j]->area.begin() + iPos);
            mPeaks[j]->binSize.erase(mPeaks[j]->binSize.begin() + iPos);
          }
          else
          {
            binMat[binMat.size() - 1][j].intensity = 0.0;
            binMat[binMat.size() - 1][j].SNR = 0.0;
            binMat[binMat.size() - 1][j].area = 0.0;
          }
        }
      }
      
      //Delete last column if it does not fit minimum filter criterion
      if(countPeaks < (int)(numOfPixels * binFilter))
      {
        binMass.erase(binMass.begin() + binMass.size() - 1);
        binSizeVec.erase(binSizeVec.begin() + binSizeVec.size() - 1);
        binAvgInt.erase(binAvgInt.begin() + binAvgInt.size() - 1);
        binMat.erase(binMat.begin() + binMat.size() - 1);
      }
      
    }
  }
  Rcout<<"Bining complete with a total number of "<<binMass.size()<<" bins\n";
  
  //Copy data to R matrices
  Rcout<<"Coping data to R object...\n";
  NumericMatrix binMatIntensity(numOfPixels, binMass.size());
  NumericMatrix binMatSNR(numOfPixels, binMass.size());
  NumericMatrix binMatArea(numOfPixels, binMass.size());
  
  //Sort columns by mass
  Rcout<<"Sorting columns by mass...\n";
  NumericVector massSorting(binMass.size());
  NumericVector binsizeSorting(binSizeVec.size());
  memcpy(massSorting.begin(), binMass.begin(), sizeof(double)*binMass.size());
  int sortedInds[binMass.size()];
  double minVal;
  for(int i = 0; i < binMass.size(); i++)
  {
    minVal = std::numeric_limits<double>::max();
    for( int j = 0; j < binMass.size(); j++ )
    {
      if( massSorting[j] < minVal && massSorting[j] > 0 )
      {
        minVal = massSorting[j];
        sortedInds[i] = j;
      }  
    }
    massSorting[ sortedInds[i]  ] = -1; //Mark as sorted
  }
  
  //Copy the mass axis sorting it
  for(int i = 0; i < binMass.size(); i++)
  {
    massSorting[i] = binMass[sortedInds[i]];
    binsizeSorting[i] = binSizeVec[sortedInds[i]];
  }
  
  //Copy the matrix sorting it
  for( int ir = 0;  ir < numOfPixels; ir++)
  {
    for(int ic = 0; ic < binMass.size(); ic++)
    {
      binMatIntensity(ir, ic ) = binMat[sortedInds[ic]][ir].intensity;
      binMatSNR      (ir, ic ) = binMat[sortedInds[ic]][ir].SNR;
      binMatArea     (ir, ic ) = binMat[sortedInds[ic]][ir].area;
    }
  }
  
  return List::create( Named("mass") = massSorting, Named("intensity") = binMatIntensity, Named("SNR") = binMatSNR, Named("area") = binMatArea, Named("binsize") = binsizeSorting );
}

// [[Rcpp::export]]
List MergePeakMatricesC( List PeakMatrices, double binningTolerance = 100, double binningFilter = 0.01 )
{
  PeakBinning myPeakBinning( NULL, 0, binningTolerance, true, binningFilter); //Must be in ppm since binsize is not available here
  for( int i = 0; i < PeakMatrices.length(); i++)
  {
    Rcout<<"Merging peak matrix "<<(i+1)<<" of "<<PeakMatrices.length()<<"\n";
    myPeakBinning.AppendPeaksAsMatrix(PeakMatrices[i]);  
  }
  return myPeakBinning.BinPeaks();
}
