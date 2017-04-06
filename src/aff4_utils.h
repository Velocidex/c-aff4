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

#ifndef SRC_AFF4_UTILS_H_
#define SRC_AFF4_UTILS_H_

#include "config.h"

#include <sys/time.h>
#include <vector>
#include <string>
#include <sstream>

#define UNUSED(x) (void)x

std::string aff4_sprintf(std::string fmt, ...);

inline uint64_t time_from_epoch() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

std::string GetLastErrorMessage();

std::vector<std::string> split(const std::string& s, char delim);

#endif  // SRC_AFF4_UTILS_H_
