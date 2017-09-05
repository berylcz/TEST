/*********************************************************************
Copyright (C) 2017 Michele Fumagalli, Mark Krumholz
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

#ifdef __INTEL_COMPILER
// Need this to fix a bug in the intel compilers relating to c++11
namespace std
{
     typedef decltype(nullptr) nullptr_t;
}
#endif

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/regex.hpp>
#include "slug_tracks_sb99.H"
#include "slug_MPI.H"
#include "constants.H"

using namespace std;
using namespace boost;
using namespace boost::algorithm;
using namespace boost::filesystem;
using namespace boost::multi_array_types;

////////////////////////////////////////////////////////////////////////
// Convenient comparator function to sort tracks into increasing age
// order
////////////////////////////////////////////////////////////////////////
namespace tracks {
  typedef struct {
    double logt;
    size_type idx;
  } track_data;
  bool tracksort (const track_data &data1, const track_data& data2) {
    return data1.logt < data2.logt;
  }
}

////////////////////////////////////////////////////////////////////////
// First constructor method: this constructor reads a single set of
// tracks from a specified file name
////////////////////////////////////////////////////////////////////////
slug_tracks_sb99::
slug_tracks_sb99(const char *fname, slug_ostreams& ostreams_) :
  slug_tracks_2d(ostreams_) {

  // Read the file header to get the number of tracks and times it
  // contains, as well as the metallicity and minimum WR mass
  size_type ntrack, ntime;
  std::ifstream trackfile
    = read_trackfile_header(fname, metallicity, WR_mass, ntrack, ntime);

  // Allocate memory to hold track data
  array1d logm;
  array2d logt;
  array3d trackdata;
  logm.resize(boost::extents[ntrack]);
  logt.resize(boost::extents[ntime+1][ntrack]);
  trackdata.resize(boost::extents[ntime+1][ntrack][tracks::nprop_sb99]);
  
  // Read the rest of the file
  read_trackfile_tracks(trackfile, logm, logt, trackdata, ntrack, ntime);

  // Specify that we want linear interpolation for the current mass,
  // and the default interpolation type for all other variables
  vector<const gsl_interp_type *> interp_type(tracks::nprop_sb99);
  for (vector<int>::size_type i=0; i<tracks::nprop_sb99; i++) {
    if (i == tracks::log_cur_mass) interp_type[i] = gsl_interp_linear;
    else interp_type[i] = slug_default_interpolator;
  }

  // Build the interpolation class that will interpolate on the tracks
  interp = new
    slug_mesh2d_interpolator_vec(logt, logm, trackdata,
				 interp_type);
}

////////////////////////////////////////////////////////////////////////
// Second constructor method; this reads a set of tracks at different
// metallicity, so that we can interpolate between them
////////////////////////////////////////////////////////////////////////
slug_tracks_sb99::
slug_tracks_sb99(const trackSet tr_set,
		 slug_ostreams& ostreams_,
		 const double metallicity_,
		 const ZInterpMethod Z_int_meth) :
  slug_tracks_2d(ostreams_, metallicity_) {

  // Make sure the interpolation method is one of the ones allowed for
  // starburst99; higher order methods are not allowed because these
  // tracks are so sparsely spaced in metallicity
  assert(Z_int_meth == Z_NEAR_NEIGHBOR || Z_int_meth == Z_LINEAR);

  // Set the file names and metallicities based on the track set
  vector<string> filenames;
  vector<double> Z;
  switch(tr_set) {
  case GENEVA_2013_VVCRIT_00: {
    filenames.push_back("Z0020v00.txt");
    filenames.push_back("Z0140v00.txt");
    Z.push_back(1./7.);
    Z.push_back(1.0);
    break;
  }
  case GENEVA_2013_VVCRIT_40: {
    filenames.push_back("Z0020v40.txt");
    filenames.push_back("Z0140v40.txt");
    Z.push_back(1./7.);
    Z.push_back(1.0);
    break;
  }
  case GENEVA_MDOT_STD: {
    filenames.push_back("modc001.dat");
    filenames.push_back("modc004.dat");
    filenames.push_back("modc008.dat");
    filenames.push_back("modc020.dat");
    filenames.push_back("modc040.dat");
    Z.push_back(0.001/0.02);
    Z.push_back(0.004/0.02);
    Z.push_back(0.008/0.02);
    Z.push_back(1.0);
    Z.push_back(2.0);
    break;
  }
  case GENEVA_MDOT_ENHANCED: {
    filenames.push_back("mode001.dat");
    filenames.push_back("mode004.dat");
    filenames.push_back("mode008.dat");
    filenames.push_back("mode020.dat");
    filenames.push_back("mode040.dat");
    Z.push_back(0.001/0.02);
    Z.push_back(0.004/0.02);
    Z.push_back(0.008/0.02);
    Z.push_back(1.0);
    Z.push_back(2.0);
    break;
  }
  case PADOVA_TPAGB_YES: {
    filenames.push_back("modp0004.dat");
    filenames.push_back("modp004.dat");
    filenames.push_back("modp008.dat");
    filenames.push_back("modp020.dat");
    filenames.push_back("modp050.dat");
    Z.push_back(0.0004/0.02);
    Z.push_back(0.004/0.02);
    Z.push_back(0.008/0.02);
    Z.push_back(1.0);
    Z.push_back(2.5);
    break;
  }
  case PADOVA_TPAGB_NO: {
    filenames.push_back("mods0004.dat");
    filenames.push_back("mods004.dat");
    filenames.push_back("mods008.dat");
    filenames.push_back("mods020.dat");
    filenames.push_back("mods050.dat");
    Z.push_back(0.0004/0.02);
    Z.push_back(0.004/0.02);
    Z.push_back(0.008/0.02);
    Z.push_back(1.0);
    Z.push_back(2.5);
    break;
  }
  }

  // Make sure metallicity is in the range covered by the tracks; if
  // not, bail out
  if (metallicity < Z.front() || metallicity > Z.back()) {
    ostreams.slug_err_one
      << "slug_tracks_sb99: requested metallicity " << metallicity
      << " is outside range of Z = " << Z.front()
      << " - " << Z.back() << " covered by requested track set"
      << endl;
    bailout(1);
  }

  // Specify that we want linear interpolation for the current mass,
  // and the default interpolation type for all other variables
  vector<const gsl_interp_type *> interp_type(tracks::nprop_sb99);
  for (vector<int>::size_type i=0; i<tracks::nprop_sb99; i++) {
    if (i == tracks::log_cur_mass) interp_type[i] = gsl_interp_linear;
    else interp_type[i] = slug_default_interpolator;
  }

  // Find the track indices that bound the input metallicity
  double wgt;
  vector<int>::size_type idx = 0;
  while (metallicity > Z[idx+1]) {
    idx++;
    if (idx == Z.size()-1) break;
  }
  wgt = log10(Z[idx+1]/metallicity);

  // Figure out which file or files we need to read
  if (Z_int_meth == Z_NEAR_NEIGHBOR) {

    // Nearest neighbor, so just figure out which file has a
    // metallicity closest to the requested one
    if (wgt > 0.5) idx = idx+1;
    
    // Read file header
    size_type ntrack, ntime;
    double Z_file;
    std::ifstream trackfile
      = read_trackfile_header(filenames[idx].c_str(), Z_file, WR_mass,
			      ntrack, ntime);
    
    // Allocate memory to hold track data
    array1d logm;
    array2d logt;
    array3d trackdata;
    logm.resize(boost::extents[ntrack]);
    logt.resize(boost::extents[ntime+1][ntrack]);
    trackdata.resize(boost::extents[ntime+1][ntrack][tracks::nprop_sb99]);
  
    // Read the rest of the file
    read_trackfile_tracks(trackfile, logm, logt, trackdata, ntrack, ntime);

    // Build the interpolation class that will interpolate on the tracks
    interp = new
      slug_mesh2d_interpolator_vec(logt, logm, trackdata,
				   interp_type);

  } else {

    // Linear interpolation

    // Read the header of the first file
    size_type ntrack, ntime;
    double Z_file;
    std::ifstream trackfile
      = read_trackfile_header(filenames[idx].c_str(), Z_file,
			      WR_mass, ntrack, ntime);

    // Allocate memory to hold track data
    array1d logm;
    array2d logt;
    array3d trackdata;
    array3d logt_Z;
    array4d trackdata_Z;
    logm.resize(boost::extents[ntrack]);
    logt_Z.resize(boost::extents[2][ntime+1][ntrack]);
    trackdata_Z.resize(boost::extents[2][ntime+1][ntrack][tracks::nprop_sb99]);
    logt.resize(boost::extents[ntime+1][ntrack]);
    trackdata.resize(boost::extents[ntime+1][ntrack][tracks::nprop_sb99]);

    // Read first file
    view2d logt_sub = logt_Z[indices[0][range_t(0,ntime+1)][range_t(0,ntrack)]];
    view3d trackdata_sub
      = trackdata_Z[indices[0][range_t(0,ntime+1)][range_t(0,ntrack)]
		    [range_t(0,tracks::nprop_sb99)]];
    read_trackfile_tracks(trackfile, logm, logt_sub, trackdata_sub,
			  ntrack, ntime);

    // Read second file  
    trackfile =
      read_trackfile_header(filenames[idx+1].c_str(), Z_file, WR_mass,
			    ntrack, ntime);
    logt_sub = logt_Z[indices[1][range_t(0,ntime+1)][range_t(0,ntrack)]];
    trackdata_sub
      = trackdata_Z[indices[1][range_t(0,ntrack)][range_t(0,ntime+1)]
		    [range_t(0,tracks::nprop_sb99)]];
    read_trackfile_tracks(trackfile, logm, logt_sub, trackdata_sub,
			  ntrack, ntime);

    // Compute weighted average
    for (size_type i=0; i<ntime+1; i++) {
      for (size_type j=0; j<ntrack; j++) {
	logt[i][j] = wgt*logt_Z[0][i][j] + (1.0-wgt)*logt_Z[1][i][j];
	for (size_type k=0; k>tracks::nprop_sb99; k++) {
	  trackdata[i][j][k] = wgt*trackdata_Z[0][i][j][k] +
	    (1.0-wgt)*trackdata_Z[1][i][j][k];
	}
      }
    }

    // Build the interpolation class that will interpolate on the tracks
    interp = new
      slug_mesh2d_interpolator_vec(logt, logm, trackdata,
				   interp_type);

  }
}

////////////////////////////////////////////////////////////////////////
// Method to read the header of a track file. Unfortunately this
// has to be compatible with starburst99, which means that a ton of
// stuff is done by hardcoding. For example, the mininum mass for WR
// formation isn't actually recorded in the data files, and is instead
// stuck in the source code. Ditto for the metallacity.
////////////////////////////////////////////////////////////////////////
std::ifstream
slug_tracks_sb99::read_trackfile_header(const char *fname,
					double& metallicity_,
					double& WR_mass_,
					size_type& ntrack,
					size_type& ntime) {
  
  // Get the metallicity from the file name
  path trackpath(fname);
  string trackpath_strip = trackpath.filename().string();

  // File names of the form modCXXX.dat or modCXXXX.dat, where C is
  // a letter and the X's are digits; metallicity is 0.XXX or 0.XXXX
  const regex pattern1("mod[A-z][0-9]{3}.dat");
  const regex pattern2("mod[A-z][0-9]{4}.dat");

  // File names of the form ZXXXXvYY.txt, where the X's and Y's are
  // digits; metallicity is 0.02 if XXXX = 0140, and is 1/7 solar if
  // XXXX = 0020
  const regex pattern3("Z0140v[0-9]{2}.txt");
  const regex pattern4("Z0020v[0-9]{2}.txt");

  // Check for matches
  match_results<std::basic_string<char>::iterator> name_match;
  if (regex_search(trackpath_strip.begin(), 
		   trackpath_strip.end(), 
		   name_match, pattern1, match_posix)) {
    string fnametmp(name_match[0].first, name_match[0].second);
    string metalstring = fnametmp.substr(4, 3);
    metalstring.insert(0, "0.");    // Add the decimal point
    metallicity_ = lexical_cast<double>(metalstring)/0.02;
  } else if (regex_search(trackpath_strip.begin(), 
			  trackpath_strip.end(), 
			  name_match, pattern2, match_posix)) {
    string fnametmp(name_match[0].first, name_match[0].second);
    string metalstring = fnametmp.substr(4, 4);
    metalstring.insert(0, "0.");    // Add the decimal point
    metallicity_ = lexical_cast<double>(metalstring)/0.02;
  } else if (regex_search(trackpath_strip.begin(), 
			  trackpath_strip.end(), 
			  name_match, pattern3, match_posix)) {
    metallicity_ = 1.0;
  } else if (regex_search(trackpath_strip.begin(), 
			  trackpath_strip.end(), 
			  name_match, pattern4, match_posix)) {
    metallicity_ = 1.0/7.0;
  } else {
    ostreams.slug_err_one
      << "unknown starburst99 file name " << fname << endl;
    bailout(1);
  }

  // Set the minimum WR mass; this is just hardcoded, as it is in SB99
  vector<string> fnames = 
    { // Geneva w/standard mass loss
      "modc001.dat", "modc004.dat", "modc008.dat", "modc020.dat",
      "modc040.dat",
      // Geneva w/high mass loss
      "mode001.dat", "mode004.dat", "mode008.dat", "mode020.dat", 
      "mode040.dat", 
      // Padova
      "mods0004.dat", "mods004.dat", "mods008.dat", "mods020.dat", 
      "mods050.dat", 
      // Padova w/AGB stars
      "modp0004.dat", "modp004.dat", "modp008.dat", "modp020.dat",
      "modp050.dat",
      // Geneva (2013) non-rotating
      "Z0020v00.txt", "Z0140v00.txt",
      // Geneva (2013) rotating at 40% of breakup
      "Z0020v40.txt", "Z0140v40.txt" };
  vector<double> wrm =
    { // Geneva w/standard mass loss
      80, 52, 42, 32, 25,
      // Geneva w/high mass loss
      61, 42, 35, 25, 21,
      // Padova
      61, 42, 35, 25, 21,
      // Padova w/AGB stars
      61, 42, 35, 25, 21,
      // Geneva (2013) non-rotating
      84, 25,
      // Geneva (2013) rotating at 40% of breakup
      55, 20 };
  WR_mass_ = -1.0;
  for (vector<double>::size_type i = 0; i<wrm.size(); i++) {
    if (trackpath.filename().string() == fnames[i]) {
      WR_mass_ = wrm[i];
      break;
    }
  }
  if (WR_mass_ < 0) {
    ostreams.slug_err_one
      << "unknown starburst99 file name " << fname << endl;
    bailout(1);
  }

  // Now try to open file
  std::ifstream trackfile;
  trackfile.open(fname);
  if (!trackfile.is_open()) {
    // Couldn't open file, so bail out
    ostreams.slug_err_one
      << "unable to open track file " 
      << fname << endl;
    bailout(1);
  }

  // Catch exceptions
  trackfile.exceptions(std::ifstream::failbit | std::ifstream::badbit | 
		       std::ifstream::eofbit);

  // Read the header
  try {
    
    // Read the track descriptor string
    string line;
    getline(trackfile, line);

    // Blank line
    getline(trackfile, line);

    // Line containing number of masses and number of times
    getline(trackfile, line);
    trim(line);
    vector<string> tokens;
    split(tokens, line, is_any_of("\t "), token_compress_on);
    try {
      ntrack = lexical_cast<unsigned long>(tokens[0]);
      ntime = lexical_cast<unsigned long>(tokens[1]);
    } catch (const bad_lexical_cast& ia) {
      (void) ia;  // No-op to suppress compiler warning
      ostreams.slug_err_one << "badly formatted starburst99 track file " 
	   << fname << endl;
      bailout(1);
    }
  } catch(std::ifstream::failure e) {
    (void) e;  // No-op to suppress compiler warning
    ostreams.slug_err_one << "badly formatted starburst99 track file " 
			  << fname << endl;
    bailout(1);
  }

  // Return pointer to file
  return trackfile;
}


////////////////////////////////////////////////////////////////////////
// Method to read the tracks from a sb99 track file, and store the
// data in the arguments passed in
////////////////////////////////////////////////////////////////////////
template<typename S, typename T>
void slug_tracks_sb99::
read_trackfile_tracks(std::ifstream& trackfile, array1d& logm,
		      S& logt, T& trackdata, size_type ntrack,
		      size_type ntime) {
  
  try {

    // Loop over tracks
    string line;
    for (size_type i=0; i<ntrack; i++) {

      // Blank line
      getline(trackfile, line);

      // Read mass and type for track; note that tracks are stored
      // from most massive least massive, so we need to index
      // backwards
      getline(trackfile, line);
      trim(line);
      vector<string> tokens;
      split(tokens, line, is_any_of("\t "), token_compress_on);
      size_type idx = ntrack-i-1;
      try {
	logm[idx] = log(lexical_cast<double>(tokens[0]));
      } catch (const bad_lexical_cast& ia) {
	(void) ia;  // No-op to suppress compiler warning
	ostreams.slug_err_one << "badly formatted starburst99 track file " 
			      << endl;
	bailout(1);
      }
      string tracktype;
      if (tokens.size() > 1) tracktype = tokens[1];

      // Horrible hardcoding here, being forced on me by the fact that
      // this needs to be compatible with starburst99's data file
      // format, and starburst99 is written in fortran, and uses
      // fortran's godawful IO formatting techniques to break up
      // input data based on column positions. Claus, please, please
      // switch to a modern computer language... like cobol... or
      // bcpl...
      vector<unsigned int> breaks;
      if (tracktype.compare("WR") == 0) {
	unsigned int colbreaks[] = {0, 2, 16, 25, 31, 37, 46, 55, 
				    64, 73, 82, 89, 96};
	vector<unsigned int> 
	  brks(colbreaks, colbreaks + sizeof(colbreaks)/sizeof(int));
	breaks = brks;
      } else if (tracktype.compare("RO") == 0) {
	unsigned int colbreaks[] = {0, 3, 25, 37, 47, 57, 72, 87, 102,
				    117, 132, 142, 150};
	vector<unsigned int> 
	  brks(colbreaks, colbreaks + sizeof(colbreaks)/sizeof(int));
	breaks = brks;
      } else if (tracktype.compare("ML") == 0) {
	unsigned int colbreaks[] = {0, 2, 16, 25, 31, 37, 46, 55, 64, 
				    73, 82, 89};
	vector<unsigned int> 
	  brks(colbreaks, colbreaks + sizeof(colbreaks)/sizeof(int));
	breaks = brks;
      } else {
	unsigned int colbreaks[] = {0, 2, 16, 25, 31, 37, 46, 55, 
				    64, 73, 82};
	vector<unsigned int> 
	  brks(colbreaks, colbreaks + sizeof(colbreaks)/sizeof(int));
	breaks = brks;
      }

      // Blank line
      getline(trackfile, line);

      // Loop over times
      for (size_type j=1; j<=ntime; j++) {

	// Read a line
	getline(trackfile, line);

	// Assign entries to arrays
	try {

	  string dummy = line.substr(breaks[1], breaks[2]-breaks[1]);
	  trim(dummy);
	  logt[j][idx] = log(lexical_cast<double>(dummy));

	  dummy = line.substr(breaks[2], breaks[3]-breaks[2]);
	  trim(dummy);
	  trackdata[j][idx][tracks::log_cur_mass]
	    = log(lexical_cast<double>(dummy));

	  dummy = line.substr(breaks[3], breaks[4]-breaks[3]);
	  trim(dummy);
	  trackdata[j][idx][tracks::log_L] = lexical_cast<double>(dummy);

	  dummy = line.substr(breaks[4], breaks[5]-breaks[4]);
	  trim(dummy);
	  trackdata[j][idx][tracks::log_Teff] = lexical_cast<double>(dummy);

	  dummy = line.substr(breaks[5], breaks[6]-breaks[5]);
	  trim(dummy);
	  trackdata[j][idx][tracks::h_surf] = lexical_cast<double>(dummy);

	  dummy = line.substr(breaks[6], breaks[7]-breaks[6]);
	  trim(dummy);
	  trackdata[j][idx][tracks::he_surf] = lexical_cast<double>(dummy);

	  dummy = line.substr(breaks[7], breaks[8]-breaks[7]);
	  trim(dummy);
	  trackdata[j][idx][tracks::c_surf] = lexical_cast<double>(dummy);

	  dummy = line.substr(breaks[8], breaks[9]-breaks[8]);
	  trim(dummy);
	  trackdata[j][idx][tracks::n_surf] = lexical_cast<double>(dummy);

	  dummy = line.substr(breaks[9], breaks[10]-breaks[9]);
	  trim(dummy);
	  trackdata[j][idx][tracks::o_surf] = lexical_cast<double>(dummy);

	  if ((tracktype.compare("WR") == 0) || 
	      (tracktype.compare("RO") == 0)) {

	    dummy = line.substr(breaks[11], breaks[12]-breaks[11]);
	    trim(dummy);
	    trackdata[j][idx][tracks::log_mDot] = lexical_cast<double>(dummy);

	  } else if (tracktype.compare("ML") == 0) {

	    dummy = line.substr(breaks[10], breaks[11]-breaks[10]);
	    trim(dummy);
	    trackdata[j][idx][tracks::log_mDot] = lexical_cast<double>(dummy);

	  } else {
	    
	    trackdata[j][idx][tracks::log_mDot] = -30;
	    
	  }
	} catch (const bad_lexical_cast& ia) {
	  (void) ia;  // No-op to suppress compiler warning
	  ostreams.slug_err_one << "badly formatted starburst99 track file " 
				<< endl;
	  bailout(1);
	}
      }
    }
  } catch(std::ifstream::failure e) {
    (void) e;  // No-op to suppress compiler warning
    ostreams.slug_err_one << "badly formatted starburst99 track file " 
			  << endl;
    bailout(1);
  }

  // Close file
  trackfile.close();

  // Populate the dummy row at time 0. We add this row to avoid
  // running into problems if we try to interpolate to very young
  // ages.
  for (size_type i=0; i<ntrack; i++) {
    logt[0][i] = -constants::big;
    for (size_type j=0; j<trackdata.shape()[2]; j++) {
      trackdata[0][i][j] = trackdata[1][i][j];
    }
  }

  // Make sure tracks are properly sorted by age; if not, sort them
  // and issue a warning
  for (size_type i=0; i<ntrack; i++) {

    // Check if this track is properly sorted
    view1d logt_sub = logt[indices[range_t(0,ntime+1)][i]];
    if (is_sorted(logt_sub.begin(), logt_sub.end())) continue;

    auto it = is_sorted_until(logt_sub.begin(), logt_sub.end());
    cout << "not sorted at " << (*it) << endl;

    // Things are not sorted, so first print a warning
    streamsize prec = ostreams.slug_warn_one.precision();
    ostreams.slug_warn_one
      << "slug_tracks_sb99::read_trackfile_tracks: "
      << "detected non-increasing ages on track for mass "
      << exp(logm[i]) << ", log ages: ";
    bool flag_printed = false;
    for (size_type j=0; j<ntime-1; j++) {
      if (logt_sub[j] > logt_sub[j+1]) {
	if (flag_printed)
	  ostreams.slug_warn_one << "; ";
	ostreams.slug_warn_one << logt_sub[j] << ", "
			       << logt_sub[j+1];
	flag_printed = true;
      }
    }
    ostreams.slug_warn_one << "; entries will be sorted and "
			   << "calculation will continue " << endl;
    ostreams.slug_warn_one.precision(prec);

    // Now figure out how to sort the track in time order
    vector<tracks::track_data> times(ntime+1);
    for (size_type j=0; j<ntime+1; j++) {
      times[j].logt = logt[j][i];
      times[j].idx = j;
    }
    sort(times.begin(), times.end(), tracks::tracksort);

    // Replace the track data with a sorted version
    array1d logt_tmp;
    array2d trackdata_tmp;
    logt_tmp.resize(boost::extents[ntime+1]);
    trackdata_tmp.resize(boost::extents[ntime+1][trackdata.shape()[2]]);
    for (size_type j=0; j<ntime+1; j++) {
      logt_tmp[j] = logt[times[j].idx][i];
      for (size_type k=0; k<trackdata.shape()[2]; k++) {
	trackdata_tmp[j][k] = trackdata[j][i][k];
      }
    }
    for (size_type j=0; j<ntime+1; j++) {
      logt[j][i] = logt_tmp[j];
      for (size_type k=0; k<trackdata.shape()[2]; k++) {
	trackdata[j][i][k] = trackdata_tmp[j][k];
      }
    }
  }
}


////////////////////////////////////////////////////////////////////////
// Methods to determine if stars are WR stars, and, if so, what type;
// these methods assume that the isochrone is properly set for this
// age.
////////////////////////////////////////////////////////////////////////
void slug_tracks_sb99::
set_WR_type(const double m, const double t, slug_stardata& star) const {
  // If star is too cool or low mass, it is not a WR star
  if (m < WR_mass || star.logTeff < 4.4) {
    star.WR = NONE;
    return;
  }

  // If passes mass and Teff cut, check surface H fraction
  double logm = log(m);
  double logt = log(t);
  double H_frac = (*interp)(logt, logm, tracks::h_surf);
  if (H_frac > 0.4) {
    // H fraction too high to be a WR star
    star.WR = NONE;
    return;
  }

  // Star has passed mass, Teff, H fraction cuts; now check other
  // criteria to see what type of WR star
  
  // H fraction > 0.1 ==> WN
  if (H_frac > 0.1) {
    star.WR = WN;
    return;
  }

  // Check C/N ratio
  double C_frac = (*interp)(logt, logm, tracks::c_surf);
  double N_frac = (*interp)(logt, logm, tracks::n_surf);
  if (C_frac/(N_frac+constants::small) < 10.0) {
    star.WR = WN;
  } else {
    star.WR = WC;
  }  
}

void
slug_tracks_sb99::set_WR_type(const double m, 
			      spl_arr_view_1d& isochrone_,
			      acc_arr_view_1d& isochrone_acc_,
			      slug_stardata& star) const {

  // If star is too cool or low mass, it is not a WR star
  if (m < WR_mass || star.logTeff < 4.4) {
    star.WR = NONE;
    return;
  }

  // If passes mass and Teff cut, check surface H fraction
  double logm = log(m);
  double H_frac = gsl_spline_eval(isochrone_[tracks::h_surf],
				  logm,
				  isochrone_acc_[tracks::h_surf]);
  if (H_frac > 0.4) {
    // H fraction too high to be a WR star
    star.WR = NONE;
    return;
  }

  // Star has passed mass, Teff, H fraction cuts; now check other
  // criteria to see what type of WR star
  
  // H fraction > 0.1 ==> WN
  if (H_frac > 0.1) {
    star.WR = WN;
    return;
  }

  // Check C/N ratio
  double C_frac = gsl_spline_eval(isochrone_[tracks::c_surf],
				  logm,
				  isochrone_acc_[tracks::c_surf]);
  double N_frac = gsl_spline_eval(isochrone_[tracks::n_surf],
				  logm,
				  isochrone_acc_[tracks::n_surf]);
  if (C_frac/(N_frac+constants::small) < 10.0) {
    star.WR = WN;
  } else {
    star.WR = WC;
  }
}

  
  


