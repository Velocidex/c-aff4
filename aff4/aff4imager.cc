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
#include "aff4/libaff4.h"
#include "aff4/aff4_imager_utils.h"

int main(int argc, char* argv[]) {
    aff4::BasicImager imager;

    aff4::AFF4Status res = imager.Run(argc, argv);

    if (res == aff4::STATUS_OK || res == aff4::CONTINUE) {
        return 0;
    }

    imager.resolver.logger->error("Imaging failed with error: {}", AFF4StatusToString(res));

    return res;
}
