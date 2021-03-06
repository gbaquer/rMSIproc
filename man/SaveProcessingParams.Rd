% Generated by roxygen2: do not edit by hand
% Please edit documentation in R/Process.R
\name{SaveProcessingParams}
\alias{SaveProcessingParams}
\title{SaveProcessingParams.}
\usage{
SaveProcessingParams(procParams, filepath, xmlRoiFilesInclude = NULL,
  xmlRoiFilesExclude = NULL, RoiNormalization = NULL)
}
\arguments{
\item{procParams}{a list of parameters.}

\item{filepath}{a full path where params will be stored}

\item{xmlRoiFilesInclude}{a vector with the used ROI XML files for ID inclusion, NULL if no ROI was used.}

\item{xmlRoiFilesExclude}{a vector with the used ROI XML files for ID exclusion, NULL if no ROI was used.}

\item{RoiNormalization}{a string with the name of normalization used to export the data summary.}
}
\description{
Save all parameters in a list of processing params generated using ImportWizardGui() function.
Parameters will be saved in a plain text file.
}
