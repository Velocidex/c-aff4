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

BasicImager imager;

int main(int argc, char* argv[]) {
    // Initialize Google's logging library.
    google::InitGoogleLogging(argv[0]);

    google::LogToStderr();
    google::SetStderrLogging(google::GLOG_ERROR);

    AFF4Status res = imager.Run(argc, argv);
    if (res == STATUS_OK || res == CONTINUE) {
        return 0;
    }

    LOG(ERROR) << "Imaging failed with error: " << res;

    return res;
}
