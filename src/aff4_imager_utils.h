/*
  Utilities for AFF4 imaging. These are mostly high level utilities used by the
  command line imager.
*/
#ifndef _AFF4_IMAGER_UTILS_H
#define _AFF4_IMAGER_UTILS_H

AFF4Status ImageStream(DataStore &resolver, URN input_urn,
                       URN output_urn,
                       size_t buffer_size=1024*1024);

AFF4Status ExtractStream(DataStore &resolver, URN input_urn,
                         URN output_urn,
                         size_t buffer_size=1024*1024);

#endif // _AFF4_IMAGER_UTILS_H
