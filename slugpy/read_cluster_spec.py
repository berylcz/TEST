"""
Function to read a SLUG2 cluster_spec file.
"""

import numpy as np
from collections import namedtuple
import struct
from slug_open import slug_open

def read_cluster_spec(model_name, output_dir=None, fmt=None, 
                      verbose=False, read_info=None):
    """
    Function to read a SLUG2 cluster_spec file.

    Parameters
       model_name : string
          The name of the model to be read
       output_dir : string
          The directory where the SLUG2 output is located; if set to None,
          the current directory is searched, followed by the SLUG_DIR
          directory if that environment variable is set
       fmt : string
          Format for the file to be read. Allowed values are 'ascii',
          'bin' or 'binary, and 'fits'. If one of these is set, the code
          will only attempt to open ASCII-, binary-, or FITS-formatted
          output, ending in .txt., .bin, or .fits, respectively. If set
          to None, the code will try to open ASCII files first, then if
          it fails try binary files, and if it fails again try FITS
          files.
       verbose : bool
          If True, verbose output is printed as code runs
       read_info : dict
          On return, this dict will contain the keys 'fname' and
          'format', giving the name of the file read and the format it
          was in; 'format' will be one of 'ascii', 'binary', or 'fits'

    Returns
       A namedtuple containing the following fields:

       id : array, dtype uint
          unique ID of cluster
       trial: array, dtype uint
          which trial was this cluster part of
       time : array
          times at which cluster spectra are output, in yr
       wl : array
          wavelength, in Angstrom
       spec : array, shape (N_cluster, N_wavelength)
          specific luminosity of each cluster at each wavelength, in erg/s/A
       spec_neb : array, shape (N_wavelength, N_times, N_trials)
          specific luminosity at each wavelength and each time for each
          trial, including emission and absorption by the HII region,
          in erg/s/A (present only if SLUG was run with nebular
          emission enabled)
       wl_ex : array
          wavelength for the extincted spectrum, in Angstrom (present
          only if SLUG was run with extinction enabled)
       spec_ex : array, shape (N_cluster, N_wavelength)
          specific luminosity at each wavelength in wl_ex and each
          time for each trial after extinction has been applied, in
          erg/s/A (present only if SLUG was run with extinction
          enabled)
       spec_neb_ex : array, shape (N_wavelength, N_times, N_trials)
          specific luminosity at each wavelength in wl_ex and each
          time for each trial including emission and absorption by the
          HII region, after extinction has been applied, in erg/s/A
          (present only if SLUG was run with nebular emission and
          extinction both enabled)

    Raises
       IOError, if no spectrum file can be opened
    """

    # Open file
    fp, fname = slug_open(model_name+"_cluster_spec", 
                          output_dir=output_dir,
                          fmt=fmt)

    # Print status
    if verbose:
        print("Reading cluster spectra for model "+model_name)
    if read_info is not None:
        read_info['fname'] = fname

    # Prepare storage
    cluster_id = []
    time = []
    trial = []
    wavelength = []
    L_lambda = []

    # Read ASCII or binary
    if fname.endswith('.txt'):

        # ASCII mode
        if read_info is not None:
            read_info['format'] = 'ascii'

        # Read the first header line
        hdr = fp.readline()

        # See if we have extinction
        hdrsplit = hdr.split()
        if hdrsplit[-1] == 'L_lambda_ex':
            extinct = True
            wl_ex = []
            L_lambda_ex = []
        else:
            extinct = False

        # See if we have nebular emission
        if 'L_l_neb' in hdrsplit:
            nebular = True
            L_lambda_neb = []
            nebcol = hdrsplit.index('L_l_neb')
            if extinct:
                L_lambda_neb_ex = []
                nebexcol = hdrsplit.index('L_l_neb_ex')
        else:
            nebular = False

        # Burn the new two header lines
        fp.readline()
        fp.readline()

        # Read first line and store cluster data
        trialptr = 0
        entry = fp.readline()
        data = entry.split()
        cluster_id.append(long(data[0]))
        time.append(float(data[1]))
        wavelength.append(float(data[2]))
        L_lambda.append(float(data[3]))
        if nebular:
            L_lambda_neb.append(float(data[4]))
        if extinct and len(data) > 4 + nebular:
            L_lambda_ex.append(float(data[4+nebular]))
            if nebular:
                L_lambda_neb_ex.append(float(data[6]))
        trial.append(trialptr)

        # Read the rest of the data for first cluster
        while True:
            entry = fp.readline()

            # Check for EOF and separator lines
            if entry == '':
                break
            if entry[:3] == '---':
                trialptr = trialptr+1
                break

            # Split up data
            data = entry.split()
            L_lambda.append(float(data[3]))
            id_tmp = long(data[0])
            time_tmp = float(data[1])
            if nebular:
                L_lambda_neb.append(float(data[4]))
            if extinct and len(data) > 4 + nebular:
                L_lambda_ex.append(float(data[4+nebular]))
                if nebular:
                    L_lambda_neb_ex.append(float(data[6]))

            # Stop when we find a different cluster or a different time
            if id_tmp != cluster_id[0] or time_tmp != time[0]:
                break

            # Still the same cluster, so append to wavelength list
            wavelength.append(float(data[2]))
            if extinct and len(data) > 4+nebular:
                wl_ex.append(float(data[2]))

        # We have now read one full chunk, so we know how many
        # wavelength entries per cluster there are
        nl = len(wavelength)

        # Start of next chunk
        ptr = 1

        # Now read through rest of file
        while True:

            # Read a line
            entry = fp.readline()
            if entry == '':
                break
            if entry[:3] == '---':
                trialptr = trialptr+1
                continue
            data = entry.split()
            L_lambda.append(float(data[3]))
            if nebular:
                L_lambda_neb.append(float(data[4]))
            if extinct and len(data) > 4+nebular:
                L_lambda_ex.append(float(data[4+nebular]))
                if nebular:
                    L_lambda_neb_ex.append(float(data[6]))
            ptr = ptr+1

            # When we get to the end of a chunk, push cluster ID,
            # time, trial number list, then reset pointer
            if ptr == nl:
                cluster_id.append(long(data[0]))
                time.append(float(data[1]))
                trial.append(trialptr)
                ptr = 0

    elif fname.endswith('.bin'):

        # Binary mode
        if read_info is not None:
            read_info['format'] = 'binary'

        # Read a two characters to see if nebular emission and/or
        # extinction is included in this file or not
        data = fp.read(struct.calcsize('b'))
        nebular = struct.unpack('b', data)[0] != 0
        data = fp.read(struct.calcsize('b'))
        extinct = struct.unpack('b', data)[0] != 0
        if nebular:
            L_lambda_neb = []
        if extinct:
            L_lambda_ex = []
            if nebular:
                L_lambda_neb_ex = []

        # Read number of wavelengths and wavelength table
        data = fp.read(struct.calcsize('L'))
        nl, = struct.unpack('L', data)
        data = fp.read(struct.calcsize('d')*nl)
        wavelength = np.array(struct.unpack('d'*nl, data))
        if extinct:
            data = fp.read(struct.calcsize('L'))
            nl_ex, = struct.unpack('L', data)
            data = fp.read(struct.calcsize('d')*nl_ex)
            wl_ex = np.array(struct.unpack('d'*nl_ex, data))
        else:
            nl_ex = 0

        # Go through the rest of the file
        while True:

            # Read number of clusters and time in next block, checking
            # if we've hit eof
            data = fp.read(struct.calcsize('LdL'))
            if len(data) < struct.calcsize('LdL'):
                break
            trialptr, t, ncluster = struct.unpack('LdL', data)

            # Skip if no clusters
            if ncluster == 0:
                continue

            # Add to time and trial arrays
            time.extend([t]*ncluster)
            trial.extend([trialptr]*ncluster)

            # Read the next block of clusters
            data = fp.read(struct.calcsize('L')*ncluster + 
                           struct.calcsize('d')*ncluster * 
                           (1+nebular)*(nl+nl_ex))
            data_list = struct.unpack(('L'+'d'*(nl+nl_ex)*(1+nebular))
                                      * ncluster, data)

            # Pack clusters into data list
            cluster_id.extend(data_list[::(1+nebular)*(nl+nl_ex)+1])
            L_lambda.extend(
                [data_list[((1+nebular)*(nl+nl_ex)+1)*i+1 : 
                           ((1+nebular)*(nl+nl_ex)+1)*i+1+nl] 
                 for i in range(ncluster)])
            if nebular:
                L_lambda_neb.extend(
                    [data_list[((1+nebular)*(nl+nl_ex)+1)*i+1+nl : 
                               ((1+nebular)*(nl+nl_ex)+1)*i+1+2*nl] 
                     for i in range(ncluster)])
            if extinct:
                L_lambda_ex.extend(
                    [data_list[((1+nebular)*(nl+nl_ex)+1)*i+1 + 
                               nl*(1+nebular) :
                               ((1+nebular)*(nl+nl_ex)+1)*i+1 + 
                               nl*(1+nebular)+nl_ex] 
                     for i in range(ncluster)])
                if nebular:
                    L_lambda_neb_ex.extend(
                        [data_list[((1+nebular)*(nl+nl_ex)+1)*i+1 + 
                                   nl*(1+nebular) + nl_ex :
                                   ((1+nebular)*(nl+nl_ex)+1)*i+1 + 
                                   nl*(1+nebular) + 2*nl_ex] 
                         for i in range(ncluster)])

    elif fname.endswith('.fits'):

        # FITS mode
        if read_info is not None:
            read_info['format'] = 'fits'
        wavelength = fp[1].data.field('Wavelength')
        wavelength = wavelength.flatten()
        cluster_id = fp[2].data.field('UniqueID')
        trial = fp[2].data.field('Trial')
        time = fp[2].data.field('Time')
        L_lambda = fp[2].data.field('L_lambda')

        # Read nebular data if available
        if 'L_lambda_neb' in fp[2].data.columns.names:
            nebular = True
            L_lambda_neb = fp[2].data.field('L_lambda_neb')
        else:
            nebular = False

        # If we have extinction data, handle that too
        if 'Wavelength_ex' in fp[1].data.columns.names:
            extinct = True
            wl_ex = fp[1].data.field('Wavelength_ex')
            wl_ex = wl_ex.flatten()
            L_lambda_ex = fp[2].data.field('L_lambda_ex')
            if nebular:
                L_lambda_neb_ex = fp[2].data.field('L_lambda_neb_ex')
        else:
            extinct = False

    # Close file
    fp.close()

    # Convert to arrays
    wavelength = np.array(wavelength)
    cluster_id = np.array(cluster_id, dtype='uint')
    time = np.array(time)
    trial = np.array(trial, dtype='uint')
    L_lambda = np.array(L_lambda)
    L_lambda = np.reshape(L_lambda, (len(time), len(wavelength)))
    if nebular:
        L_lambda_neb = np.array(L_lambda_neb)
        L_lambda_neb = np.reshape(L_lambda_neb, (len(time), len(wavelength)))
    if extinct:
        wl_ex = np.array(wl_ex)
        L_lambda_ex = np.array(L_lambda_ex)
        L_lambda_ex = np.reshape(L_lambda_ex,
                                 (len(time), len(wl_ex)))
        if nebular:
            L_lambda_neb_ex = np.array(L_lambda_neb_ex)
            L_lambda_neb_ex = np.reshape(L_lambda_neb_ex, 
                                         (len(time), len(wl_ex)))

    # Build namedtuple to hold output
    fieldnames = ['id', 'trial', 'time', 'wl', 'spec']
    fields = [cluster_id, trial, time, wavelength, L_lambda]
    if nebular:
        fieldnames = fieldnames + ['spec_neb']
        fields = fields + [L_lambda_neb]
    if extinct:
        fieldnames = fieldnames + ['wl_ex', 'spec_ex']
        fields = fields + [wl_ex, L_lambda_ex]
        if nebular:
            fieldnames = fieldnames + ['spec_neb_ex']
            fields = fields + [L_lambda_neb_ex]
    out_type = namedtuple('integrated_spec', fieldnames)
    out = out_type(*fields)

    # Return
    return out
