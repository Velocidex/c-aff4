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

#ifndef AFF4_RDF_H_
#define AFF4_RDF_H_

#include <string>
#include <memory>
#include "string.h"
#include "aff4_utils.h"
#include "aff4_errors.h"
#include <raptor2/raptor2.h>

using std::string;
using std::unique_ptr;
using std::vector;

// Base class for all RDF Values. An RDFValue is an object which knows how to
// serialize and unserialize itself from a DataStoreObject.
class RDFValue {
 public:
  virtual raptor_term *GetRaptorTerm(raptor_world *world) const {
    return NULL;
  };

  // RDFValues must provide methods for serializing and unserializing.
  virtual string SerializeToString() const {
    return "";
  };

  virtual AFF4Status UnSerializeFromString(const char *data, int length) {
    return GENERIC_ERROR;
  };

  AFF4Status UnSerializeFromString(const string data) {
    return UnSerializeFromString(data.data(), data.size());
  };

  AFF4Status Set(const string data) {
    return UnSerializeFromString(data.c_str(), data.size());
  };

  virtual ~RDFValue(){};
};

static const char* const lut = "0123456789ABCDEF";

/* These are the objects which are stored in the data store */
class RDFBytes: public RDFValue {
 public:
  string value;

  RDFBytes(string data): RDFBytes(data.c_str(), data.size()) {};

  RDFBytes(const char * data, unsigned int length): value(data, length) {}

  RDFBytes() {};

  string SerializeToString() const;
  AFF4Status UnSerializeFromString(const char *data, int length);
  raptor_term *GetRaptorTerm(raptor_world *world) const;
};


class XSDString: public RDFBytes {
 public:
  XSDString(string data): RDFBytes(data.c_str(), data.size()) {};
  XSDString(const char * data): RDFBytes(data, strlen(data)) {};
  XSDString() {};

  string SerializeToString() const;
  AFF4Status UnSerializeFromString(const char *data, int length);
  raptor_term *GetRaptorTerm(raptor_world *world) const;
};

class XSDInteger: public RDFValue {
 public:
  unsigned long long int value;

  XSDInteger(int data): value(data) {};
  XSDInteger() {};

  string SerializeToString() const;

  AFF4Status UnSerializeFromString(const char *data, int length);

  raptor_term *GetRaptorTerm(raptor_world *world) const;
};


class URN: public XSDString {
 public:
  URN(const char * data): XSDString(data) {};
  URN(string data): XSDString(data) {};
  URN() {};
  raptor_term *GetRaptorTerm(raptor_world *world) const;
};

#endif // AFF4_RDF_H_
