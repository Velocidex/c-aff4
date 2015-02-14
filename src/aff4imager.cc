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
#include "aff4_imager_utils.h"
#include <glog/logging.h>
#include <iostream>
#include <algorithm>

// Supports all integer inputs given as hex.
#define TCLAP_SETBASE_ZERO 1
#include <tclap/CmdLine.h>

using namespace TCLAP;
using namespace std;


AFF4Status parseOptions(int argc, char** argv) {
  MemoryDataStore resolver;

  try {
    CmdLine cmd("AFF4 Imager", ' ', AFF4_VERSION);

    SwitchArg view("V", "view", "View AFF4 metadata", false);
    cmd.add(view);

    SwitchArg verbose("v", "verbose", "Display more verbose logging", false);
    cmd.add(verbose);

    SwitchArg truncate(
        "t", "truncate", "Truncate the output file. Normally volumes and "
        "images are appended to existing files, but this flag forces the "
        "output file to be truncated first.", false);
    cmd.add(truncate);

    ValueArg<string> input(
        "i", "in", "File to image. If specified we copy this file to the "
        "output volume located at --output. If there is no AFF4 volume on "
        "--output yet, we create a new volume on it.",
        false, "",
        "/path/to/file/or/device");
    cmd.add(input);

    ValueArg<string> export_(
        "e", "export", "Name of the stream to export. If specified we try "
        "to open this stream and write it to the --output file. Note that "
        "you will also need to specify an AFF4 volume path to load so we know "
        "where to find the stream. If there is only one volume loaded then a "
        "relative URN implies a stream residing in that volume.", false,
        "", "string");
    cmd.add(export_);

    ValueArg<string> output(
        "o", "out", "Output file to write to. If the file does not "
        "exist we create it.", false, "",
        "/path/to/file");
    cmd.add(output);

    UnlabeledMultiArg<string> aff4_volumes(
        "fileName", "These AFF4 Volumes will be loaded and their metadata will "
        "be parsed before the program runs.",
        false, "/path/to/aff4/volume");
    cmd.add(aff4_volumes);

    //
    // Parse the command line.
    //
    cmd.parse(argc,argv);

    // Added debugging level.
    if(verbose.isSet()) {
      google::SetStderrLogging(google::GLOG_INFO);
    };


    // Preload existing AFF4 Volumes.
    if(aff4_volumes.isSet()) {
      vector<string> v = aff4_volumes.getValue();
      for (unsigned int i = 0; i < v.size(); i++) {
        LOG(INFO) << "Preloading AFF4 Volume: " << v[i];
        AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, v[i]);
        if(zip->members.size() == 0) {
          LOG(ERROR) << "Unable to load " << v[i]
                     << " as an existing AFF4 Volume.";
          return IO_ERROR;
        };
      };
    };

    // Dump all info.
    if(view.isSet()) {
      resolver.Dump(verbose.getValue());
    };

    // Handle output mode.
    if(output.isSet()) {
      // We are allowed to write on the output file.
      if(truncate.isSet()) {
        LOG(INFO) << "Truncating output file: " << output.getValue() << "\n";
        resolver.Set(output.getValue(), AFF4_STREAM_WRITE_MODE,
                     new XSDString("truncate"));
      } else {
        resolver.Set(output.getValue(), AFF4_STREAM_WRITE_MODE,
                     new XSDString("append"));
      };
    };

    // Imaging mode.
    if(input.isSet()) {
      if(!output.isSet()) {
        cout << "ERROR: Can not specify an input without an output\n";
        return INVALID_INPUT;
      };

      return ImageStream(resolver, input.getValue(), output.getValue());
    };

    // Extraction mode.
    if(export_.isSet()) {
      if(!output.isSet()) {
        cout << "ERROR: Can not specify an export without an output\n";
        return INVALID_INPUT;
      };

      return ExtractStream(resolver, export_.getValue(), output.getValue());
    };

  } catch (ArgException& e) {
    cout << "ERROR: " << e.error() << " " << e.argId() << endl;
  }

  return STATUS_OK;
}


int main(int argc, char* argv[]) {
  // Initialize Google's logging library.
  google::InitGoogleLogging(argv[0]);

  google::LogToStderr();
  google::SetStderrLogging(google::GLOG_ERROR);

  AFF4Status res = parseOptions(argc, argv);
  if (res == STATUS_OK)
    return 0;

  return res;
}
