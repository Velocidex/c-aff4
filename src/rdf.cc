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

#include "aff4_base.h"
#include "lexicon.h"
#include "rdf.h"
#include <limits.h>
#include <pcre++.h>
#include <stdlib.h>
#include <unistd.h>
#include <uriparser/Uri.h>

#ifdef _WIN32
#include <shlwapi.h>
#endif

static string _NormalizePath(const string &component);

string RDFBytes::SerializeToString() const {
  string result;

  result.resize(value.size() * 2 + 1);

  for(unsigned int i=0; i< value.size(); i++) {
    const unsigned char c = value[i];
    result.push_back(lut[c >> 4]);
    result.push_back(lut[c & 15]);
  };

  return result;
};

AFF4Status RDFBytes::UnSerializeFromString(const char *data, int length) {
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
};

raptor_term *RDFBytes::GetRaptorTerm(raptor_world *world) const {
  string value_string(SerializeToString());

  return raptor_new_term_from_counted_literal(
      world,
      (const unsigned char *)value_string.c_str(),
      value_string.size(),
      NULL,
      NULL, 0);
};


string XSDString::SerializeToString() const {
  return string(value.data(), value.size());
};

AFF4Status XSDString::UnSerializeFromString(const char *data, int length) {
  value = string(data, length);

  return STATUS_OK;
};


raptor_term *XSDString::GetRaptorTerm(raptor_world *world) const {
  string value_string(SerializeToString());
  raptor_uri *uri = raptor_new_uri(
      world, (const unsigned char *)XSD_NAMESPACE "string");

  raptor_term *result= raptor_new_term_from_counted_literal(
      world,
      (const unsigned char *)value_string.c_str(),
      value_string.size(),
      uri,
      NULL, 0);

  raptor_free_uri(uri);

  return result;
};


uri_components URN::Parse() const {
  UriParserStateA state;
  UriUriA uri;
  uri_components result;

  state.uri = &uri;

  if(uriParseUriA(&state, value.c_str()) != URI_SUCCESS) {
    LOG(INFO) << "Failed to parse " << value << " as a URN";
  } else {
    result.scheme.assign(
        uri.scheme.first, uri.scheme.afterLast - uri.scheme.first);

    result.domain.assign(
        uri.hostText.first, uri.hostText.afterLast - uri.hostText.first);

    result.fragment.assign(
        uri.fragment.first, uri.fragment.afterLast - uri.fragment.first);

    for(auto it=uri.pathHead; it!=0; it=it->next)
      result.path += "/" + string(
          it->text.first, it->text.afterLast - it->text.first);
  }

  uriFreeUriMembersA(&uri);
  return result;
};


URN::URN(const char *data): URN() {
  UriParserStateA state;
  UriUriA uri;

  state.uri = &uri;

  if(uriParseUriA(&state, data) != URI_SUCCESS) {
    LOG(INFO) << "Failed to parse " << data << " as a URN";
  };

  // No scheme specified - assume this is a filename.
  if(uri.pathHead && !uri.scheme.first) {
    value = NewURNFromFilename(data).SerializeToString();
    goto exit;
  };

  if (uriNormalizeSyntaxA(&uri) != URI_SUCCESS) {
    LOG(INFO) << "Failed to normalize";
  }

  int charsRequired;
  if (uriToStringCharsRequiredA(&uri, &charsRequired) == URI_SUCCESS) {
    charsRequired++;

    char tmp[charsRequired * sizeof(char)];
    if (uriToStringA(tmp, &uri, charsRequired, NULL) != URI_SUCCESS) {
      LOG(INFO) << "Failed";
    };

    value = tmp;
  };

exit:
  uriFreeUriMembersA(&uri);
};


raptor_term *URN::GetRaptorTerm(raptor_world *world) const {
  string value_string(SerializeToString());

  return raptor_new_term_from_counted_uri_string(
      world,
      (const unsigned char *)value_string.c_str(),
      value_string.size());
};

URN URN::Append(const string &component) const {
  return URN(value + _NormalizePath(component));
};


string URN::RelativePath(const URN other) const {
  string my_urn = SerializeToString();
  string other_urn = other.SerializeToString();

  if (0 == my_urn.compare(0, my_urn.size(), other_urn,
                          0, my_urn.size())) {
    return other_urn.substr(my_urn.size(), string::npos);
  };

  return other_urn;
};



static string _NormalizePath(const string &component) {
  vector<string> result;
  size_t i = 0, j = 0;

  while(j < component.size()) {
    j = component.find("/", i);
    if (j == std::string::npos)
      j = component.size();

    string sub_component = component.substr(i, j - i);
    i = j + 1;
    if (sub_component == "..") {
      if(!result.empty()) {
        result.pop_back();
      };

      continue;
    };

    if (sub_component == "." || sub_component == "") {
      continue;
    };

    result.push_back(sub_component);
  };

  string result_component = "/";
  for(auto sub_component: result) {
    result_component.append(sub_component);
    result_component.append("/");
  };

  result_component.pop_back();
  return result_component;
};


#ifdef _WIN32

/**
 * Windows implementation of abspath. Use the system APIs to normalize the path
 * name.
 *
 * @param path
 *
 * @return an absolute path.
 */
static string abspath(string path) {
  // The windows version of this function is somewhat simpler.
  DWORD buffer_len = GetFullPathName(path.c_str(), 0, NULL, NULL);
  if (buffer_len > 0) {
    char buffer[buffer_len];
    GetFullPathName(path.c_str(), buffer_len, buffer, NULL);
    return buffer;
  };

  return path;
}

/* Windows filename -> URL handling is pretty complex. The urlparser library
 * does a reasonable job but misses some important edge cases. Microsoft
 * recommends that we use the provided API to cater to all weird edge cases.
 */
string URN::ToFilename() {
  // Alas Microsoft's implementation is also incomplete. Here we check for some
  // edge cases and manually hack around them.
  pcrepp::Pcre volume_regex("^file://./([a-zA-Z]):$");  // file://./c: -> \\.\c:
  if(volume_regex.search(value))
    return volume_regex.replace(value, "\\\\.\\$1:");

  const int bytesNeeded = std::max(value.size() + 1, (size_t)MAX_PATH);
  char path[bytesNeeded];
  DWORD path_length = sizeof(path);
  HRESULT res;

  res = PathCreateFromUrl(value.c_str(), path, &path_length, 0);
  if(res == S_FALSE || res == S_OK) {
    LOG(INFO) << "Converted " << value << " into " << path;
    return path;

    // Failing the MS API we fallback to the urlparser.
  } else {
    if (uriUriStringToWindowsFilenameA(value.c_str(), path) !=
      URI_SUCCESS) {
      return "";
    };

    return path;
  }
};

#else

/**
 * Posix implementation of abspath: prepend cwd and normalize path.
 *
 * @param path
 *
 * @return
 */
static string abspath(string path) {
  // Path is absolute.
  if (path[0] == '/' ||
      path[0] == '\\' ||
      path[1] == ':') {
    return path;
  };

  // Prepend the CWD to the path.
  int path_len = PATH_MAX;
  while(1) {
    char cwd[path_len];

    // Try again with a bigger size.
    if(NULL==getcwd(cwd, path_len) && errno == ERANGE) {
      path_len += PATH_MAX;
      continue;
    };

    // Remove . and .. sequences.
    return _NormalizePath(string(cwd) + "/" + path);
  };
};

// Unix version to ToFilename().
string URN::ToFilename() {
  const int bytesNeeded = value.size() + 1;
  char path[bytesNeeded];

  if (uriUriStringToUnixFilenameA(value.c_str(), path) !=
      URI_SUCCESS) {
    return "";
  };

  return path;
};

#endif


URN URN::NewURNFromOSFilename(string filename, bool windows_filename,
                              bool absolute_path) {
  URN result;

  if (absolute_path)
    filename = abspath(filename);

  char tmp[filename.size() * 3 + 8 + 1];

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
        result.value = tmp;
      };
    };

    // Unix filename
  } else if (uriUnixFilenameToUriStringA(filename.c_str(), tmp) ==
            URI_SUCCESS) {
    result.value = tmp;
  };

  return result;
};


URN URN::NewURNFromFilename(string filename, bool absolute_path) {
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
    };
  }

  return NewURNFromOSFilename(filename, windows_filename, absolute_path);
};

string XSDInteger::SerializeToString() const {
    return aff4_sprintf("%lld", value);
};

AFF4Status XSDInteger::UnSerializeFromString(const char *data, int length) {
  string s_data = string(data, length);
  const char *start = s_data.c_str();
  char *end;

  errno = 0;
  value = strtoll(start, &end, 0);

  if (errno != 0 || *end != 0) {
    return PARSING_ERROR;
  };

  return STATUS_OK;
};


raptor_term *XSDInteger::GetRaptorTerm(raptor_world *world) const {
  string value_string(SerializeToString());
  raptor_uri *uri = raptor_new_uri(
      world, (const unsigned char *)XSD_NAMESPACE "integer");

  raptor_term *result= raptor_new_term_from_counted_literal(
      world,
      (const unsigned char *)value_string.c_str(),
      value_string.size(),
      uri,
      NULL, 0);

  raptor_free_uri(uri);

  return result;
};


string XSDBoolean::SerializeToString() const {
  return value ? "true": "false";
};

AFF4Status XSDBoolean::UnSerializeFromString(const char *data, int length) {
  string s_data = string(data, length);
  if (s_data == "true" || s_data == "1") {
    value = true;
  } else if(s_data == "false" || s_data == "0") {
    value = false;
  } else {
    return PARSING_ERROR;
  };

  return STATUS_OK;
};


raptor_term *XSDBoolean::GetRaptorTerm(raptor_world *world) const {
  string value_string(SerializeToString());
  raptor_uri *uri = raptor_new_uri(
      world, (const unsigned char *)XSD_NAMESPACE "boolean");

  raptor_term *result= raptor_new_term_from_counted_literal(
      world,
      (const unsigned char *)value_string.c_str(),
      value_string.size(),
      uri,
      NULL, 0);

  raptor_free_uri(uri);

  return result;
};


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
