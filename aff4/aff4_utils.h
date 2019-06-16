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

#include <codecvt>
#include <locale>

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

    inline bool IsAFF4Container(std::string filename) noexcept {
        // Cheap nasty not really unicode transformation to lower case.
        std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);
        return hasEnding(filename, ".af4") || hasEnding(filename, ".aff4");
    }

    /**
     * Does the file entity exist, and is a regular file.
     * @param name The filename to check
     * @return TRUE if the file entity exists and is a regular file.
     */
    inline bool IsFile(const std::string& name) {
#ifndef _WIN32
        /*
         * POSIX based systems.
         */
        struct stat buffer;
        if (::stat(name.c_str(), &buffer) == 0) {
            return S_ISREG(buffer.st_mode);
        }
        return false;
#else 
        /*
         * Windows based systems
         */
        std::wstring filename = s2ws(name);
        DWORD dwAttrib = GetFileAttributes(filename.c_str());
        return (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));

#endif
    }

    inline std::wstring s2ws(const std::string& str) {
        using convert_typeX = std::codecvt_utf8<wchar_t>;
        std::wstring_convert<convert_typeX, wchar_t> converterX;
        return converterX.from_bytes(str);
    }

    inline std::string ws2s(const std::wstring& wstr) {
        using convert_typeX = std::codecvt_utf8<wchar_t>;
        std::wstring_convert<convert_typeX, wchar_t> converterX;
        return converterX.to_bytes(wstr);
    }

} // namespace aff4

#endif  // SRC_AFF4_UTILS_H_
