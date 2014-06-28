/*********************************************************************
Copyright (C) 2014 Robert da Silva, Michele Fumagalli, Mark Krumholz
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/

#include "slug_PDF.H"
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <cstdlib>

using namespace boost;
using namespace boost::algorithm;
using namespace boost::filesystem;
using namespace boost::random;


////////////////////////////////////////////////////////////////////////
// Constructor
////////////////////////////////////////////////////////////////////////
slug_PDF::slug_PDF(const char *PDF, rng_type &rng) {

  // Try to open the PDF file; search for it relative to SLUG_DIR
  // environment variable first if that is set, and then search
  // relative to current directory
  ifstream PDFFile;
  char *slug_dir = getenv("SLUG_DIR");
  path PDFpath(PDF), PDFfullPath;
  if (slug_dir != NULL) {
    // Try opening relative to SLUG_DIR
    PDFfullPath = path(slug_dir) / PDFpath;
    PDFFile.open(PDFfullPath.string());
  }
  if (PDFFile.is_open()) {
    PDFpath = PDFfullPath;
  } else {
    // Try opening relative to current path
    PDFFile.open(PDFpath.string());
  }
  if (!PDFFile.is_open()) {
    // Couldn't open file, so bail out
    cerr << "slug error: unable to open PDF file " 
	 << PDFpath.string();
    if (slug_dir != NULL)
      cerr << " or " << PDFfullPath.string();
    cerr << endl;
    exit(1);
  }

  // Save name of PDF file
  PDFFileName = PDFpath.string();

  // We've successfully opened the PDF file. Read the first
  // non-whitespace line.
  string line, linecopy;
  int lineCount = 0;
  while (!PDFFile.eof()) {
    getline(PDFFile, line);
    linecopy = line;
    lineCount++;
    trim_left(line);
    if ((line.compare(0, 1, "#") != 0) && 
	(line.length() != 0)) break;
  }

  // Tokenize the line by whitespace and commas
  vector<string> tokens;
  split(tokens, line, is_any_of("\t ,"), token_compress_on);

  // Look at the first token
  to_lower(tokens[0]);
  if (tokens[0].compare("breakpoints")==0) {
    // First token is "breakpoints", so pass to basic mode parser
    parseBasic(PDFFile, tokens, lineCount, rng);
  } else if (tokens[0].compare("advanced")==0) {
    // First token is "advanced". Make sure there's no extraneous junk
    // on this line, and then call advanced mode parser
    if (tokens.size() > 1) {
      if (tokens[1].compare(0, 1, "#") != 0) {
	parseError(lineCount, linecopy, 
		   "Expected: 'breakpoints' or 'advanced'");
      }
    }
    parseAdvanced(PDFFile, lineCount, rng);
  } else {
    // First word is not breakpoints or advanced, so bail out
    parseError(lineCount, linecopy, 
	       "Expected: 'breakpoints' or 'advanced'");
  }

  // Close file
  PDFFile.close();

  // Set up the discrete distribution picker
  if (segments.size() > 1) {
    discrete_distribution<> dist(weights.begin(), weights.end());
    variate_generator<rng_type&, discrete_distribution <> >  
      var_disc(rng, dist);
    disc = &var_disc;
  }
}


////////////////////////////////////////////////////////////////////////
// Destructor
////////////////////////////////////////////////////////////////////////
slug_PDF::~slug_PDF() { 
  for (int i=0; i<segments.size(); i++)
    delete segments[i];
}


////////////////////////////////////////////////////////////////////////
// Draw function
////////////////////////////////////////////////////////////////////////
double
slug_PDF::draw() {

  // First decide which segment to draw from
  int segNum;
  if (segments.size() > 1) {
    segNum = (*disc)();
  } else {
    segNum = 1;
  }

  // Draw from that segment and return
  return(segments[segNum]->draw());
}


////////////////////////////////////////////////////////////////////////
// Basic parser
////////////////////////////////////////////////////////////////////////
void
slug_PDF::parseBasic(ifstream& PDFFile, vector<string> firstline,
		     int& lineCount, rng_type &rng) {

  // First token of first line is breakpoints; make sure that we have
  // at least tokens total on that line
  if (firstline.size() < 3)
    parseError(lineCount, "", "Need at least two breakpoints");

  // Read the breakpoints
  int nbreak = firstline.size() - 1;
  int nsegment = nbreak - 1;
  double *breakpoints = new double[nbreak];
  for (int i=0; i<nbreak; i++) {
    try {
      breakpoints[i] = stod(firstline[i+1]);
    } catch (const invalid_argument& ia) {
      // If we're here, a type conversion failed
      parseError(lineCount, "", 
		 "Expected: 'breakpoints M1 M2 M3 ... MN'");
    }
  }

  // Now read the segments associated with those breakpoints. Each
  // segment should be formatted as
  // segment
  // type TYPE
  // var1 VALUE
  // var2 VALUE
  // ...
  // Where the names of var1, var2, etc. depend on the type of segment
  bool inSegment = false;
  string line, linecopy;
  vector<string> tokens;
  while (!PDFFile.eof()) {

    // Get a line and trim leading whitespace
    getline(PDFFile, line);
    lineCount++;
    linecopy = line;
    trim_left(line);

    // Skip comment and blank lines
    if (line.length() == 0) continue;
    if (line.compare(0, 1, "#") == 0) continue;

    // Split line into tokens, and lowercase the first one
    split(tokens, line, is_any_of("\t ,"), token_compress_on);
    to_lower(tokens[0]);

    // Action depends on whether we're in a segment
    if (inSegment) {

      // We are reading a segment, so figure out what kind

      // Make sure this is a type specification
      if (tokens[0].compare("type") != 0)
	parseError(lineCount, linecopy, "Expected: 'type TYPE'");

      // Make sure there's no extraneous junk on the line
      if (tokens.size() > 2) {
	if (tokens[2].compare(0, 1, "#") != 0) {
	  parseError(lineCount, linecopy, "Expected: 'type TYPE'");
	}
      }

      // Read the segment type, and call the appropriate constructor
      to_lower(tokens[1]);
      slug_PDF_segment *seg = NULL;
      if (tokens[1].compare("lognormal")==0) {
	slug_PDF_lognormal *new_seg = new slug_PDF_lognormal;
	seg = (slug_PDF_segment *) new_seg;
      } else if (tokens[1].compare("normal")==0) {
	slug_PDF_normal *new_seg = new slug_PDF_normal;
	seg = (slug_PDF_segment *) new_seg;
      } else if (tokens[1].compare("powerlaw")==0) {
	slug_PDF_powerlaw *new_seg = new slug_PDF_powerlaw;
	seg = (slug_PDF_segment *) new_seg;
      } else if (tokens[1].compare("schechter")==0) {
	slug_PDF_schechter *new_seg = new slug_PDF_schechter;
	seg = (slug_PDF_segment *) new_seg;
      } else {
	string errStr("Unknown segment type ");
	errStr += tokens[1];
	parseError(lineCount, linecopy, errStr);
      }

      // Call the parser for the segment we just created to get
      // whatever data it needs
      string errMsg;
      parseStatus stat = seg->parse(PDFFile, lineCount, errMsg, rng);
      if (stat == PARSE_ERROR)
	parseError(lineCount, "", errMsg);
      else if (stat == EOF_ERROR)
	eofError(errMsg);

      // Push this segment onto the segment vector
      segments.push_back(seg);
      weights.push_back(1.0);   // We'll fix the weights below

      // We're done with this segment
      inSegment = false;

    } else {

      // If we're not in the middle of a segment, the first token
      // should be "segment" and no other non-comment tokens should be
      // present. Check that this is the case.
      if (tokens[0].compare("segment") != 0)
	parseError(lineCount, linecopy, "Expected: 'segment'");
      if (tokens.size() > 1) {
	if (tokens[1].compare(0, 1, "#") != 0) {
	  parseError(lineCount, linecopy, "Expected: 'segment'");
	}
      }

      // We've found a valid segment line, so set a flag
      inSegment = true;
    }
  }

  // Make sure we have the right number of segments; if not, throw
  // error
  if (segments.size() != nsegment) {
    string errStr = "Expected " + lexical_cast<string>(nsegment) +
      " segments, found " + lexical_cast<string>(segments.size());
    parseError(lineCount, "", errStr);
  }

  // Now figure out the correct weights on all segments in order to
  // make them continuous across the breakpoints
  double cumWeight = weights[0];
  for (int i=1; i<nsegment; i++) {
    weights[i] = weights[i-1] * 
      segments[i-1]->sMaxVal() / segments[i]->sMinVal();
    cumWeight += weights[i];
  }
  for (int i=0; i<nsegment; i++)
    weights[i] /= cumWeight;
}


////////////////////////////////////////////////////////////////////////
// Advanced parser
////////////////////////////////////////////////////////////////////////
void
slug_PDF::parseAdvanced(ifstream& PDFFile, int& lineCount, 
			rng_type &rng) {

  // Advanced files are formatted as a series of segments, each of
  // which follows the format
  // segment
  // type TYPE
  // weight WEIGHT
  // var1 VALUE
  // var2 VALUE
  // ...
  // Where the names of var1, var2, etc. depend on the type of segment
  string line, linecopy;
  vector<string> tokens;
  bool inSegment = false;
  while (!PDFFile.eof()) {

    // Get a line and trim leading whitespace
    getline(PDFFile, line);
    lineCount++;
    linecopy = line;
    trim_left(line);

    // Skip comment and blank lines
    if (line.length() == 0) continue;
    if (line.compare(0, 1, "#") == 0) continue;

    // Split line into tokens, and lowercase the first one
    split(tokens, line, is_any_of("\t ,"), token_compress_on);
    to_lower(tokens[0]);

    // Action depends on whether we're in a segment
    if (inSegment) {

      // We are reading a segment, so figure out what kind

      // Make sure this is a type specification
      if (tokens[0].compare("type") != 0)
	parseError(lineCount, linecopy, "Expected: 'type TYPE'");

      // Make sure there's no extraneous junk on the line
      if (tokens.size() > 2) {
	if (tokens[2].compare(0, 1, "#") != 0) {
	  parseError(lineCount, linecopy, "Expected: 'type TYPE'");
	}
      }

      // Read the segment type, and call the appropriate constructor
      to_lower(tokens[1]);
      slug_PDF_segment *seg = NULL;
      if (tokens[1].compare("lognormal")==0) {
	slug_PDF_lognormal *new_seg = new slug_PDF_lognormal;
	seg = (slug_PDF_segment *) &new_seg;
      } else if (tokens[1].compare("normal")==0) {
	slug_PDF_normal *new_seg = new slug_PDF_normal;
	seg = (slug_PDF_segment *) &new_seg;
      } else if (tokens[1].compare("powerlaw")==0) {
	slug_PDF_powerlaw *new_seg = new slug_PDF_powerlaw;
	seg = (slug_PDF_segment *) &new_seg;
      } else if (tokens[1].compare("schechter")==0) {
	slug_PDF_schechter *new_seg = new slug_PDF_schechter;
	seg = (slug_PDF_segment *) &new_seg;
      } else {
	string errStr("Unknown segment type ");
	errStr += tokens[1];
	parseError(lineCount, linecopy, errStr);
      }

      // Call the parser for the segment we just created to get
      // whatever data it needs
      string errMsg;
      double wgt;
      parseStatus stat = seg->parse(PDFFile, lineCount, errMsg, rng,
				    &wgt);
      if (stat == PARSE_ERROR)
	parseError(lineCount, "", errMsg);
      else if (stat == EOF_ERROR)
	eofError(errMsg);

      // Push this segment onto the segment vector
      segments.push_back(seg);
      weights.push_back(wgt);

    } else {

      // If we're not in the middle of a segment, the first token
      // should be "segment" and no other non-comment tokens should be
      // present. Check that this is the case.
      if (tokens[0].compare("segment") != 0)
	parseError(lineCount, linecopy, "Expected: 'segment'");
      if (tokens.size() > 1) {
	if (tokens[1].compare(0, 1, "#") != 0) {
	  parseError(lineCount, linecopy, "Expected: 'segment'");
	}
      }

      // We've found a valid segment line, so set a flag
      inSegment = true;
    }
  }

  // Make sure we got at least one segment. If not, bail out.
  if (segments.size()==0)
    eofError("Expected to find at least 1 segment.");

  // Normalize segment weights
  double cumWeight = weights[0];
  for (int i=1; i<segments.size(); i++) {
    cumWeight += weights[i];
  }
  for (int i=0; i<segments.size(); i++)
    weights[i] /= cumWeight;
}


////////////////////////////////////////////////////////////////////////
// Parsing error handler
////////////////////////////////////////////////////////////////////////
void
  slug_PDF::parseError(int lineCount, string line, string message) {
  cerr << "slug error: parsing error in file " 
       << PDFFileName 
       << " on line " << lineCount;
  if (line.length() > 0) 
    cerr << ": " << endl << line << endl;
  else
    cerr << endl;
  if (message.length() > 0)
    cerr << message << endl;
  exit(1);
}


////////////////////////////////////////////////////////////////////////
// Unexpected EOF error handler
////////////////////////////////////////////////////////////////////////
void
slug_PDF::eofError(string message) {
  cerr << "slug error: unxepctedly reached end of PDF file "
       << PDFFileName << endl;
  if (message.length() > 0)
    cerr << message << endl;
  exit(1);
}
