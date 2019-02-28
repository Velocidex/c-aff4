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

#include "aff4/config.h"

#include <vector>
#include <string>
#include <sstream>

#include "spdlog/spdlog.h"

namespace aff4 {

#define UNUSED(x) (void)x

std::string aff4_sprintf(std::string fmt, ...);

std::string GetLastErrorMessage();

std::vector<std::string> split(const std::string& s, char delim);

std::shared_ptr<spdlog::logger> get_logger();

#define RETURN_IF_ERROR(expr)                   \
    do {                                        \
        AFF4Status res = (expr);                \
        if (res != STATUS_OK) {                 \
            printf("%s: at %s: %d\n", AFF4StatusToString(res), __FILE__, __LINE__); \
            return res;                         \
        };                                      \
    } while (0);

// A portable version of fnmatch.
int fnmatch(const char *pattern, const char *string);

inline bool hasEnding(std::string const &fullString, std::string const &ending) {
    if (fullString.length() >= ending.length()) {
        return (0 == fullString.compare(
                    fullString.length() - ending.length(),
                    ending.length(), ending));
    } else {
         return false;
    }
}


} // namespace aff4

#endif  // SRC_AFF4_UTILS_H_
