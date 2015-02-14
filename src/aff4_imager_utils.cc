/*
  Utilities for AFF4 imaging. These are mostly high level utilities used by the
  command line imager.
*/
#include "libaff4.h"
#include "aff4_imager_utils.h"
#include <iostream>
#include <time.h>

using std::cout;

AFF4Status ImageStream(DataStore &resolver, URN input_urn,
                       URN output_urn,
                       size_t buffer_size) {
  AFF4ScopedPtr<AFF4Stream> input = resolver.AFF4FactoryOpen<AFF4Stream>(input_urn);
  AFF4ScopedPtr<AFF4Stream> output = resolver.AFF4FactoryOpen<AFF4Stream>(output_urn);

  if(!input) {
    LOG(ERROR) << "Failed to open input file: " << input_urn.value.c_str()
               << ".\n";
    return IO_ERROR;
  };

  if(!output) {
    LOG(ERROR) << "Failed to create output file: " << output_urn.value.c_str()
               << ".\n";
    return IO_ERROR;
  };

  AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, output->urn);
  if(!zip) {
    return IO_ERROR;
  };

  // Create a new image in this volume.
  URN image_urn = zip->urn.Append(input_urn.Parse().path);

  AFF4ScopedPtr<AFF4Image> image = AFF4Image::NewAFF4Image(
      &resolver, image_urn, zip->urn);

  if(!image) {
    return IO_ERROR;
  };

  while(1) {
    string data = input->Read(buffer_size);
    if(data.size() == 0) {
      break;
    };

    image->Write(data);
  };

  return STATUS_OK;
};


AFF4Status ExtractStream(DataStore &resolver, URN input_urn,
                         URN output_urn,
                         size_t buffer_size) {
  AFF4ScopedPtr<AFF4Stream> input = resolver.AFF4FactoryOpen<AFF4Stream>(
      input_urn);
  AFF4ScopedPtr<AFF4Stream> output = resolver.AFF4FactoryOpen<AFF4Stream>(
      output_urn);

  if(!input) {
    LOG(ERROR) << "Failed to open input stream. " << input_urn.value.c_str()
               << ".\n";
    return IO_ERROR;
  };

  if(!output) {
    LOG(ERROR) << "Failed to create output file: " << output_urn.value.c_str()
               << ".\n";
    return IO_ERROR;
  };

  time_t last_time = 0;
  while(1) {
    string data = input->Read(buffer_size);
    if(data.size() == 0) {
      break;
    };

    output->Write(data);

    time_t now = time(NULL);

    if (now > last_time) {
      cout << output->Size()/1024/1024 << "MiB / " << input->Size()/1024/1024
           << "MiB (" << 100 * output->Size() / input->Size() << "%) \r";
      cout.flush();
      last_time = now;
    };
  };

  return STATUS_OK;
};
