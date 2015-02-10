/*
Copyright 2014 Google Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use
this file except in compliance with the License.  You may obtain a copy of the
License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.  See the License for the
specific language governing permissions and limitations under the License.
*/

/*
  This is the command line tool to manager aff4 image volumes and acquire
  images.
*/
#include "libaff4.h"
#include <glog/logging.h>
#include <gflags/gflags.h>
#include <iostream>

DEFINE_bool(aff4_version, false, "Print version of AFF4 library.");

DEFINE_bool(view, false, "Dump information about all streams. This command "
            "should be followed by a list of AFF4 volumes to open.");

DEFINE_bool(image, false, "Switch on imaging mode. The infile and outfile "
            "parameters are expected.");

DEFINE_string(infile, "-", "The filename to read or '-' for stdin.");
DEFINE_string(outfile, "-", "The filename to write on or '-' for stdout.");


int main(int argc, char* argv[]) {
  // Initialize Google's logging library.
  google::InitGoogleLogging(argv[0]);

  google::SetUsageMessage("AFF4 Imager.");
  google::SetVersionString(AFF4_VERSION);

  google::ParseCommandLineFlags(&argc, &argv, true);

  MemoryDataStore resolver;

  // First load all volumes provided. If we fail we die here.
  for(int i=1; i<argc; i++) {
    AFF4ScopedPtr<AFF4Stream> backing_file = resolver.AFF4FactoryOpen<AFF4Stream>(
        argv[i]);

    if(!backing_file->Size()) {
      LOG(ERROR) << "Unable to open " << argv[i] << "\n";
      exit(-1);
    };

    ZipFile::NewZipFile(&resolver, argv[i]);
  };


  if (FLAGS_aff4_version) {
    std::cout << "AFF4 Library version: " << AFF4_VERSION << "\n";
    exit(0);
  } else if (FLAGS_view) {
    resolver.Dump();
    exit(0);
  } else if (FLAGS_image) {
    LOG(INFO) << "Imaging mode selected." << "\n";
  };

  // If we get here just show the short help.
  char *newargv[2] = {argv[0], (char *)"-helpshort"};
  char **newargv_p = (char **)&newargv;
  int newargc = 2;

  google::ParseCommandLineFlags(&newargc, &newargv_p, false);
}
