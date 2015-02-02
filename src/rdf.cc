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

#include <pcre++.h>
#include "lexicon.h"
#include "rdf.h"

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


uri_components::uri_components(const string &uri) {
  static pcrepp::Pcre uri_regex(
      "^(([^:/?#]+):)?(//([^/?#]*))?([^?#]*)(\\\\?([^#]*))?(#(.*))?");

  if (uri_regex.search(uri)) {
    scheme = uri_regex[1];
    path = uri_regex[3];
    if(path == "") {
      path = uri_regex[4];
    };
  };

  if (scheme == "") {
    scheme = "file";
  };
};


uri_components URN::Parse() const {
  return uri_components(value);
};

raptor_term *URN::GetRaptorTerm(raptor_world *world) const {
  string value_string(SerializeToString());

  return raptor_new_term_from_counted_uri_string(
      world,
      (const unsigned char *)value_string.c_str(),
      value_string.size());
};

URN URN::Append(const string &component) {
  return URN(value + "/" + component);
};


string XSDInteger::SerializeToString() const {
    return aff4_sprintf("%ld", value);
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

// A Global Registry for RDFValue. This factory will provide the correct
// RDFValue instance based on the turtle type URN. For example xsd:integer ->
// XSDInteger().
ClassFactory<RDFValue> RDFValueRegistry;


static RDFValueRegistrar<RDFBytes> r1(XSD_NAMESPACE "hexBinary");
static RDFValueRegistrar<XSDString> r2(XSD_NAMESPACE "string");

static RDFValueRegistrar<XSDInteger> r3(XSD_NAMESPACE "integer");
static RDFValueRegistrar<XSDInteger> r4(XSD_NAMESPACE "int");
static RDFValueRegistrar<XSDInteger> r5(XSD_NAMESPACE "long");
