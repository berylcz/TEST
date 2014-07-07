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

#include "slug_cluster.H"
#include "slug_galaxy.H"
#include "slug_parmParser.H"
#include "slug_tracks.H"
#include <cassert>
#include <iomanip>

using namespace boost::filesystem;

////////////////////////////////////////////////////////////////////////
// A trivial little helper function, used below
////////////////////////////////////////////////////////////////////////
bool sort_death_time_decreasing(const slug_star star1, 
				const slug_star star2) {
  return (star1.death_time > star2.death_time);
}

////////////////////////////////////////////////////////////////////////
// The constructor
////////////////////////////////////////////////////////////////////////
slug_galaxy::slug_galaxy(slug_parmParser& pp, slug_PDF* my_imf,
			 slug_PDF* my_cmf, slug_PDF* my_sfh,
			 slug_tracks* my_tracks, 
			 slug_PDF* my_clf) {

  // Record distributions and tracks to be used in this problem
  imf = my_imf;
  cmf = my_cmf;
  sfh = my_sfh;
  clf = my_clf;
  tracks = my_tracks;

  // Initialize mass and time
  curTime = 0.0;
  mass = 0.0;
  targetMass = 0.0;
  aliveMass = 0.0;
  clusterMass = 0.0;

  // Get fc
  fc = pp.get_fClust();

  // Initialize the cluster ID pointer
  cluster_id = 0;

  // Store output mode
  out_mode = pp.get_outputMode();

  // Open integrated properties file and write its header
  if (pp.get_writeIntegratedProp()) {
    string fname(pp.get_modelName());
    fname += "_integrated_prop";
    path full_path(pp.get_outDir());
    if (out_mode == ASCII) {
      fname += ".txt";
      full_path /= fname;
      int_prop_file.open(full_path.c_str(), ios::out);
    } else if (out_mode == BINARY) {
      fname += ".bin";
      full_path /= fname;
      int_prop_file.open(full_path.c_str(), ios::out | ios::binary);
    }
    if (!int_prop_file.is_open()) {
      cerr << "slug error: unable to open intergrated properties file " 
	   << full_path.string() << endl;
      exit(1);
    }
    if (out_mode == ASCII) {
      int_prop_file << setw(14) << left << "Time"
		    << setw(14) << left << "TargetMass"
		    << setw(14) << left << "ActualMass"
		    << setw(14) << left << "LiveMass"
		    << setw(14) << left << "ClusterMass"
		    << setw(14) << left << "NumClusters"
		    << setw(14) << left << "NumDisClust"
		    << setw(14) << left << "NumFldStar"
		    << endl;
      int_prop_file << setw(14) << left << "(yr)"
		    << setw(14) << left << "(Msun)"
		    << setw(14) << left << "(Msun)"
		    << setw(14) << left << "(Msun)"
		    << setw(14) << left << "(Msun)"
		    << setw(14) << left << ""
		    << setw(14) << left << ""
		    << setw(14) << left << ""
		    << endl;
      int_prop_file << setw(14) << left << "-----------"
		    << setw(14) << left << "-----------"
		    << setw(14) << left << "-----------"
		    << setw(14) << left << "-----------"
		    << setw(14) << left << "-----------"
		    << setw(14) << left << "-----------"
		    << setw(14) << left << "-----------"
		    << setw(14) << left << "-----------"
		    << endl;
    }
  }

  // Open cluster properties file and write its header
  if (pp.get_writeClusterProp()) {
    string fname(pp.get_modelName());
    fname += "_cluster_prop";
    path full_path(pp.get_outDir());
    if (out_mode == ASCII) {
      fname += ".txt";
      full_path /= fname;
      cluster_prop_file.open(full_path.c_str(), ios::out);
    } else if (out_mode == BINARY) {
      fname += ".bin";
      full_path /= fname;
      cluster_prop_file.open(full_path.c_str(), ios::out | ios::binary);
    }
    if (!cluster_prop_file.is_open()) {
      cerr << "slug error: unable to open cluster properties file " 
	   << full_path.string() << endl;
      exit(1);
    }
    if (out_mode == ASCII) {
      cluster_prop_file << setw(14) << left << "UniqueID"
			<< setw(14) << left << "Time"
			<< setw(14) << left << "FormTime"
			<< setw(14) << left << "Lifetime"
			<< setw(14) << left << "TargetMass"
			<< setw(14) << left << "BirthMass"
			<< setw(14) << left << "LiveMass"
			<< setw(14) << left << "NumStar"
			<< setw(14) << left << "MaxStarMass"
			<< endl;
      cluster_prop_file << setw(14) << left << ""
			<< setw(14) << left << "(yr)"
			<< setw(14) << left << "(yr)"
			<< setw(14) << left << "(yr)"
			<< setw(14) << left << "(Msun)"
			<< setw(14) << left << "(Msun)"
			<< setw(14) << left << "(Msun)"
			<< setw(14) << left << ""
			<< setw(14) << left << "(Msun)"
			<< endl;
      cluster_prop_file << setw(14) << left << "-----------"
			<< setw(14) << left << "-----------"
			<< setw(14) << left << "-----------"
			<< setw(14) << left << "-----------"
			<< setw(14) << left << "-----------"
			<< setw(14) << left << "-----------"
			<< setw(14) << left << "-----------"
			<< setw(14) << left << "-----------"
			<< setw(14) << left << "-----------"
			<< endl;
    }
  }
}


////////////////////////////////////////////////////////////////////////
// Destructor
////////////////////////////////////////////////////////////////////////
slug_galaxy::~slug_galaxy() {

  // Destroy cluster lists
  while (disrupted_clusters.size() > 0) {
    delete disrupted_clusters.back();
    disrupted_clusters.pop_back();
  }
  while (clusters.size() > 0) {
    delete clusters.back();
    clusters.pop_back();
  }

  // Close open files
  if (int_prop_file.is_open()) int_prop_file.close();
  if (cluster_prop_file.is_open()) cluster_prop_file.close();
}


////////////////////////////////////////////////////////////////////////
// Reset function -- sets galaxy back to initial state
////////////////////////////////////////////////////////////////////////
void
slug_galaxy::reset(bool reset_cluster_id) {
  // N.B. By default we do NOT reset the cluster_id pointer here,
  // because if we're doing multiple trials, we want the cluster IDs
  // for different trials to be distinct.
  curTime = mass = targetMass = aliveMass = clusterMass = 0.0;
  field_stars.resize(0);
  while (disrupted_clusters.size() > 0) {
    delete disrupted_clusters.back();
    disrupted_clusters.pop_back();
  }
  while (clusters.size() > 0) {
    delete clusters.back();
    clusters.pop_back();
  }
  if (reset_cluster_id) cluster_id = 0;
}


////////////////////////////////////////////////////////////////////////
// Advance routine
////////////////////////////////////////////////////////////////////////
void
slug_galaxy::advance(double time) {

  // Make sure we're not trying to go back into the past
  assert(time >= curTime);

  // Compute mass of new stars to be created
  double new_mass = sfh->integral(curTime, time);
  targetMass += new_mass;

  // Create new clusters
  if (fc != 0) {

    // Get masses of new clusters
    vector<double> new_cluster_masses;
    cmf->drawPopulation(fc*new_mass, new_cluster_masses);

    // Create clusters of chosen masses; for each one, generate a
    // random birth time, create the cluster, and push it onto the
    // master cluster list
    for (unsigned int i=0; i<new_cluster_masses.size(); i++) {
      double birth_time = sfh->draw(curTime, time);
      slug_cluster *new_cluster = 
	new slug_cluster(cluster_id++, new_cluster_masses[i],
			 birth_time, imf, tracks, clf);
      clusters.push_back(new_cluster);
      mass += new_cluster->get_birth_mass();
      aliveMass += new_cluster->get_birth_mass();
      clusterMass += new_cluster->get_birth_mass();
    }
  }

  // Create new field stars
  if (fc != 1) {

    // Get masses of new field stars
    vector<double> new_star_masses;
    imf->drawPopulation((1.0-fc)*new_mass, new_star_masses);

    // Push stars onto field star list; in the process, set the birth
    // time and death time for each of them
    for (unsigned int i=0; i<new_star_masses.size(); i++) {
      slug_star new_star;
      new_star.mass = new_star_masses[i];
      new_star.birth_time = sfh->draw(curTime, time);
      new_star.death_time = new_star.birth_time 
	+ tracks->star_lifetime(new_star.mass);
      field_stars.push_back(new_star);
      mass += new_star.mass;
      aliveMass += new_star.mass;
    }

    // Sort field star list by death time, from largest to smallest
    sort(field_stars.begin(), field_stars.end(), 
	 sort_death_time_decreasing);
  }

  // Advance all clusters to current time; track how the currently
  // alive star mass in the galaxy changes due to this evolution
  list<slug_cluster *>::iterator it;
  for (it = clusters.begin(); it != clusters.end(); it++) {
    aliveMass -= (*it)->get_alive_mass();
    clusterMass -= (*it)->get_alive_mass();
    (*it)->advance(time);
    aliveMass += (*it)->get_alive_mass();
    clusterMass += (*it)->get_alive_mass();
  }
  for (it = disrupted_clusters.begin(); 
       it != disrupted_clusters.end(); it++) {
    aliveMass -= (*it)->get_alive_mass();
    (*it)->advance(time);
    aliveMass += (*it)->get_alive_mass();
  }

  // See if any clusters were disrupted over the last time step, and,
  // if so, move them to the disrupted list
  it=clusters.begin();
  while (it != clusters.end()) {
    if ((*it)->disrupted()) {
      disrupted_clusters.push_back(*it);
      clusterMass -= (*it)->get_alive_mass();
      it = clusters.erase(it);
    } else {
      ++it;
    }
  }

  // Go through the field star list and remove any field stars that
  // have died
  int i = field_stars.size() - 1;
  if (i >= 0) {
    while (field_stars[i].death_time < curTime) {
      aliveMass -= field_stars.back().mass;
      field_stars.pop_back();
      i--;
      if (i<0) break;
    }
  }

  // Store new time
  curTime = time;
}


////////////////////////////////////////////////////////////////////////
// Output integrated properties
////////////////////////////////////////////////////////////////////////
void
slug_galaxy::write_integrated_prop() {

  if (out_mode == ASCII) {
    int_prop_file << setprecision(5) << scientific 
		  << setw(11) << right << curTime << "   "
		  << setw(11) << right << targetMass << "   "
		  << setw(11) << right << mass << "   "
		  << setw(11) << right << aliveMass << "   "
		  << setw(11) << right << clusterMass << "   "
		  << setw(11) << right << clusters.size() << "   "
		  << setw(11) << right << disrupted_clusters.size() << "   "
		  << setw(11) << right << field_stars.size()
		  << endl;
  } else {
    int_prop_file.write((char *) &curTime, sizeof curTime);
    int_prop_file.write((char *) &mass, sizeof mass);
    int_prop_file.write((char *) &aliveMass, sizeof aliveMass);
    int_prop_file.write((char *) &clusterMass, sizeof clusterMass);
    vector<slug_cluster *>::size_type n = clusters.size();
    int_prop_file.write((char *) &n, sizeof n);
    n = disrupted_clusters.size();
    int_prop_file.write((char *) &n, sizeof n);
    n = field_stars.size();
    int_prop_file.write((char *) &n, sizeof n);
  }
}


////////////////////////////////////////////////////////////////////////
// Output cluster properties properties
////////////////////////////////////////////////////////////////////////
void
slug_galaxy::write_cluster_prop() {
  for (list<slug_cluster *>::iterator it = clusters.begin();
       it != clusters.end(); ++it)
    (*it)->write_prop(cluster_prop_file, out_mode);
}