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

#include "aff4/aff4_base.h"
#include "aff4/lexicon.h"
#include "aff4/rdf.h"
#include <limits.h>
#include <regex>
#include <stdlib.h>
#include <unistd.h>
#include <cerrno>
#include <spdlog/fmt/ostr.h>
#include <uriparser/Uri.h>

#ifdef _WIN32
#include <shlwapi.h>
#endif

namespace aff4 {

static std::string _NormalizePath(const std::string& component);

std::string RDFBytes::SerializeToString() const {
    std::string result;

    result.resize(value.size() * 2 + 1);

    for(unsigned int i=0; i< value.size(); i++) {
        const unsigned char c = value[i];
        result.push_back(lut[c >> 4]);
        result.push_back(lut[c & 15]);
    };

    return result;
}

AFF4Status RDFBytes::UnSerializeFromString(const char* data, int length) {
    // Length is odd.
    if (length & 1) {
        return INVALID_INPUT;
    };

    value.clear();

    for (int i = 0; i < length; i += 2) {
        char a = data[i];
        const char* p = std::lower_bound(lut, lut + 16, a);
        if (*p != a) {
            return INVALID_INPUT;
        };

        char b = data[i + 1];
        const char* q = std::lower_bound(lut, lut + 16, b);
        if (*q != b) {
            return INVALID_INPUT;
        };

        value.push_back(((p - lut) << 4) | (q - lut));
    }

    return STATUS_OK;
}


std::ostream& operator<<(std::ostream& os, const RDFValue& c) {
    return os << c.SerializeToString();
}

raptor_term* RDFBytes::GetRaptorTerm(raptor_world* world) const {
    std::string value_string(SerializeToString());

    return raptor_new_term_from_counted_literal(
               world,
               (const unsigned char*)value_string.c_str(),
               value_string.size(),
               nullptr,
               nullptr, 0);
}


std::string XSDString::SerializeToString() const {
    return std::string(value.data(), value.size());
}

AFF4Status XSDString::UnSerializeFromString(const char* data, int length) {
    value = std::string(data, length);

    return STATUS_OK;
}


raptor_term* XSDString::GetRaptorTerm(raptor_world* world) const {
    std::string value_string(SerializeToString());
    raptor_uri* uri = raptor_new_uri(
        world, (const unsigned char*)XSDStringType);

    raptor_term* result= raptor_new_term_from_counted_literal(
                             world,
                             (const unsigned char*)value_string.c_str(),
                             value_string.size(),
                             uri,
                             nullptr, 0);

    raptor_free_uri(uri);

    return result;
}

raptor_term* MD5Hash::GetRaptorTerm(raptor_world* world) const {
    std::string value_string(SerializeToString());
    raptor_uri* uri = raptor_new_uri(
        world, (const unsigned char*)AFF4_HASH_MD5);

    raptor_term* result= raptor_new_term_from_counted_literal(
                             world,
                             (const unsigned char*)value_string.c_str(),
                             value_string.size(),
                             uri,
                             nullptr, 0);

    raptor_free_uri(uri);

    return result;
}

raptor_term* SHA1Hash::GetRaptorTerm(raptor_world* world) const {
    std::string value_string(SerializeToString());
    raptor_uri* uri = raptor_new_uri(
        world, (const unsigned char*)AFF4_HASH_SHA1);

    raptor_term* result= raptor_new_term_from_counted_literal(
                             world,
                             (const unsigned char*)value_string.c_str(),
                             value_string.size(),
                             uri,
                             nullptr, 0);

    raptor_free_uri(uri);

    return result;
}

raptor_term* SHA256Hash::GetRaptorTerm(raptor_world* world) const {
    std::string value_string(SerializeToString());
    raptor_uri* uri = raptor_new_uri(
        world, (const unsigned char*)AFF4_HASH_SHA256);

    raptor_term* result= raptor_new_term_from_counted_literal(
                             world,
                             (const unsigned char*)value_string.c_str(),
                             value_string.size(),
                             uri,
                             nullptr, 0);

    raptor_free_uri(uri);

    return result;
}

raptor_term* SHA512Hash::GetRaptorTerm(raptor_world* world) const {
    std::string value_string(SerializeToString());
    raptor_uri* uri = raptor_new_uri(
        world, (const unsigned char*)AFF4_HASH_SHA512);

    raptor_term* result= raptor_new_term_from_counted_literal(
                             world,
                             (const unsigned char*)value_string.c_str(),
                             value_string.size(),
                             uri,
                             nullptr, 0);

    raptor_free_uri(uri);

    return result;
}

raptor_term* Blake2BHash::GetRaptorTerm(raptor_world* world) const {
    std::string value_string(SerializeToString());
    raptor_uri* uri = raptor_new_uri(
        world, (const unsigned char*)AFF4_HASH_BLAKE2B);

    raptor_term* result= raptor_new_term_from_counted_literal(
                             world,
                             (const unsigned char*)value_string.c_str(),
                             value_string.size(),
                             uri,
                             nullptr, 0);

    raptor_free_uri(uri);

    return result;
}



std::string URN::Scheme() const {
    // We only support aff4 and file URNs:
    if (value.compare(0, strlen(AFF4_PREFIX), AFF4_PREFIX) == 0) {
        return "aff4";
    }

    if (value.compare(0, strlen(FILE_PREFIX), FILE_PREFIX) == 0) {
        return "file";
    }

    if (value.compare(0, strlen(BUILTIN_PREFIX), BUILTIN_PREFIX) == 0) {
        return "builtin";
    }

    return "";
}

std::string URN::Path() const {
    if (Scheme() == "file") {
        return value.substr(strlen(FILE_PREFIX));
    }

    if (Scheme() == "aff4") {
        // Calculate offset of any trailing path
        const auto offset = strlen(AFF4_PREFIX) + Domain().size() + 1;

        // Some valid URNs don't have paths
        if (value.size() > offset) {
            return value.substr(offset);
        }
    }

    return "";
}

std::string URN::Domain() const {
    if (Scheme() == "aff4" ) {
        auto components = split(
            value.substr(strlen(AFF4_PREFIX)), '/');
        if (components.size() > 0) {
            return components[0];
        }
    }
    if (Scheme() == "builtin" ) {
        auto components = split(
            value.substr(strlen(BUILTIN_PREFIX)), '/');
        if (components.size() > 0) {
            return components[0];
        }
    }

    return "";
}

URN::URN(const char* data): URN() {
    value = std::string(data);
}


raptor_term* URN::GetRaptorTerm(raptor_world* world) const {
    std::string value_string(SerializeToString());

    return raptor_new_term_from_counted_uri_string(
               world,
               (const unsigned char*)value_string.c_str(),
               value_string.size());
}

URN URN::Append(const std::string& component) const {
    int i = value.size()-1;
    while (i > 0 && (value[i] == '/' || value[i] == '\\')) {
        i--;
    }

    const std::string urn = value.substr(0, i+1) + _NormalizePath(component);
    return URN(urn);
}


std::string URN::RelativePath(const URN other) const {
    std::string my_urn = SerializeToString();
    std::string other_urn = other.SerializeToString();

    if (0 == my_urn.compare(0, my_urn.size(), other_urn,
                            0, my_urn.size())) {
        return other_urn.substr(my_urn.size(), std::string::npos);
    }

    return other_urn;
}


static std::string _NormalizePath(const std::string& component) {
    std::vector<std::string> result;
    size_t i = 0, j = 0;

    while (j < component.size()) {
        j = component.find(URN_PATH_SEPARATOR, i);
        if (j == std::string::npos) {
            j = component.size();
        }

        std::string sub_component = component.substr(i, j - i);
        i = j + 1;
        if (sub_component == "..") {
            if (!result.empty()) {
                result.pop_back();
            }

            continue;
        }

        if (sub_component == "." || sub_component == "") {
            continue;
        }

        result.push_back(sub_component);
    }

    std::string result_component = URN_PATH_SEPARATOR;
    for (auto sub_component: result) {
        result_component.append(sub_component);
        result_component.append(URN_PATH_SEPARATOR);
    }

    result_component.pop_back();
    return result_component;
}


#ifdef _WIN32

/**
 * Windows implementation of abspath. Use the system APIs to normalize the path
 * name.
 *
 * @param path
 *
 * @return an absolute path.
 */
static std::string abspath(std::string path) {
    // The windows version of this function is somewhat simpler.
    DWORD buffer_len = GetFullPathName(path.c_str(), 0, NULL, NULL);
    if (buffer_len > 0) {
        auto buffer = std::unique_ptr<TCHAR>(new TCHAR[buffer_len]);
        GetFullPathName(path.c_str(), buffer_len, buffer.get(), NULL);
        return std::string(buffer.get());
    }

    return path;
}

/* Windows filename -> URL handling is pretty complex. The urlparser library
 * does a reasonable job but misses some important edge cases. Microsoft
 * recommends that we use the provided API to cater to all weird edge cases.
 */
std::string URN::ToFilename() const {
    // Alas Microsoft's implementation is also incomplete. Here we check for some
    // edge cases and manually hack around them.
    std::regex volume_regex("^file://./([a-zA-Z]):$");  // file://./c: -> \\.\c:
    if (std::regex_match(value, volume_regex)) {
        return std::regex_replace(value, volume_regex, "\\\\.\\$1:");
    }

    const int bytesNeeded = std::max(value.size() + 1, (size_t)MAX_PATH);
    auto path = std::unique_ptr<char>(new char[bytesNeeded]);
    DWORD path_length = bytesNeeded;
    HRESULT res;

    res = PathCreateFromUrl(value.c_str(), path.get(), &path_length, 0);
    if (res == S_FALSE || res == S_OK) {
        return std::string(path.get());

        // Failing the MS API we fallback to the urlparser.
    } else {
        if (uriUriStringToWindowsFilenameA(value.c_str(), path.get()) !=
            URI_SUCCESS) {
            return "";
        }

        return std::string(path.get());
    }
}

#else

/**
 * Posix implementation of abspath: prepend cwd and normalize path.
 *
 * @param path
 *
 * @return
 */
static std::string abspath(std::string path) {
    // Path is absolute.
    if (path[0] == '/' ||
            path[0] == '\\' ||
            path[1] == ':') {
        return path;
    }

    // Prepend the CWD to the path.
    int path_len = PATH_MAX;
    while (1) {
        std::unique_ptr<char[]> cwd (new char[path_len]);

        // Try again with a bigger size.
        if (nullptr==getcwd(cwd.get(), path_len) && errno == ERANGE) {
            path_len += PATH_MAX;
            continue;
        }

        // Remove . and .. sequences.
        return _NormalizePath(
            std::string(cwd.get()) + URN_PATH_SEPARATOR + path);
    }
}

// Unix version to ToFilename().
std::string URN::ToFilename() const {
    const int bytesNeeded = value.size() + 1;
    std::unique_ptr<char[]>  path (new char[bytesNeeded]);

    if (uriUriStringToUnixFilenameA(value.c_str(), path.get()) != URI_SUCCESS) {
        return "";
    }
    return std::string(path.get());
}

#endif


URN URN::NewURNFromOSFilename(std::string filename, bool windows_filename,
                              bool absolute_path) {
    URN result;

    if (absolute_path) {
        filename = abspath(filename);
    }

    char* tmp = new char[filename.size() * 3 + 8 + 1];

    /* Windows filename -> URL handling is pretty complex. The urlparser library
     * does a reasonable job but misses some important edge cases. Microsoft
     * recommends that we use the provided API to cater to all weird edge cases
     * since windows filenames are a rats nest of special cases and exceptions. So
     * on windows we try to use the API.
     *
     * http://blogs.msdn.com/b/ie/archive/2006/12/06/file-uris-in-windows.aspx
    */
    if (windows_filename) {
#ifdef _WIN32
        char url[INTERNET_MAX_URL_LENGTH];
        DWORD url_length = sizeof(url);
        HRESULT res;
        res = UrlCreateFromPath(filename.c_str(), url, &url_length, 0);

        if (res == S_FALSE || res == S_OK) {
            result.value.assign(url, url_length);

            // Failing the MS API we fallback to the urlparser.
        } else
#endif
        {
            if (uriWindowsFilenameToUriStringA(
                        filename.c_str(), tmp) == URI_SUCCESS) {
                result.value = std::string(tmp);
            }
        };

        // Unix filename
    } else if (uriUnixFilenameToUriStringA(filename.c_str(), tmp) ==
               URI_SUCCESS) {
        result.value = std::string(tmp);
    }
    delete[] tmp;
    return result;
}


URN URN::NewURNFromFilename(std::string filename, bool absolute_path) {
#ifdef _WIN32
    bool windows_filename = true;
#else
    bool windows_filename = false;

    // FIXME: Due to a bug in uriparser handling of relative paths, we currently
    // force all UNIX path names to be absolute.
    absolute_path = true;
#endif

    // Get the absolute path of the filename.
    if (absolute_path) {
        filename = abspath(filename);
        if (filename[0] != '/') {
            windows_filename = true;
        }
    }

    return NewURNFromOSFilename(filename, windows_filename, absolute_path);
}

std::string XSDInteger::SerializeToString() const {
    return aff4_sprintf("%lld", value);
}

AFF4Status XSDInteger::UnSerializeFromString(const char* data, int length) {
    std::string s_data = std::string(data, length);
    const char* start = s_data.c_str();
    char* end;

    errno = 0;
    value = strtoll(start, &end, 0);

    if (errno != 0 || *end != 0) {
        return PARSING_ERROR;
    }

    return STATUS_OK;
}


raptor_term* XSDInteger::GetRaptorTerm(raptor_world* world) const {
    std::string value_string(SerializeToString());
    raptor_uri* uri = raptor_new_uri(
        world, (const unsigned char*)XSDIntegerType);

    raptor_term* result = raptor_new_term_from_counted_literal(
        world,
        (const unsigned char*)value_string.c_str(),
        value_string.size(),
        uri,
        nullptr, 0);

    raptor_free_uri(uri);

    return result;
}


std::string XSDBoolean::SerializeToString() const {
    return value ? "true": "false";
}

AFF4Status XSDBoolean::UnSerializeFromString(const char* data, int length) {
    std::string s_data = std::string(data, length);
    if (s_data == "true" || s_data == "1") {
        value = true;
    } else if (s_data == "false" || s_data == "0") {
        value = false;
    } else {
        return PARSING_ERROR;
    }

    return STATUS_OK;
}


raptor_term* XSDBoolean::GetRaptorTerm(raptor_world* world) const {
    std::string value_string(SerializeToString());
    raptor_uri* uri = raptor_new_uri(
        world, (const unsigned char*)XSDBooleanType);

    raptor_term* result = raptor_new_term_from_counted_literal(
        world,
        (const unsigned char*)value_string.c_str(),
        value_string.size(),
        uri,
        nullptr, 0);

    raptor_free_uri(uri);

    return result;
}


// A Global Registry for RDFValue. This factory will provide the correct
// RDFValue instance based on the turtle type URN. For example xsd:integer ->
// XSDInteger().
ClassFactory<RDFValue> RDFValueRegistry;


static RDFValueRegistrar<RDFBytes> r1(RDFBytesType);
static RDFValueRegistrar<XSDString> r2(XSDStringType);

static RDFValueRegistrar<XSDInteger> r3(XSDIntegerType);
static RDFValueRegistrar<XSDInteger> r4(XSDIntegerTypeInt);
static RDFValueRegistrar<XSDInteger> r5(XSDIntegerTypeLong);
static RDFValueRegistrar<XSDBoolean> r6(XSDBooleanType);

static RDFValueRegistrar<MD5Hash> r7(AFF4_HASH_MD5);
static RDFValueRegistrar<SHA1Hash> r8(AFF4_HASH_SHA1);
static RDFValueRegistrar<SHA256Hash> r9(AFF4_HASH_SHA256);
static RDFValueRegistrar<SHA512Hash> r10(AFF4_HASH_SHA512);
static RDFValueRegistrar<Blake2BHash> r11(AFF4_HASH_BLAKE2B);

} // namespace aff4
