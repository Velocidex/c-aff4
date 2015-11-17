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

#ifndef  SRC_RDF_H_
#define  SRC_RDF_H_

#include <string>
#include <memory>
#include "string.h"
#include "aff4_utils.h"
#include "aff4_errors.h"
#include <raptor2/raptor2.h>
#include "aff4_registry.h"
#include <uriparser/Uri.h>

using std::string;
using std::shared_ptr;
using std::unique_ptr;
using std::vector;

/**
 * @file
 * @author scudette <scudette@localhost>
 * @date   Mon Jan 19 09:52:39 2015
 *
 * @brief  Define some common RDF value types.
 *
 *
 */

class DataStore;
class URN;

/**
 * An RDFValue object is one which knows how to serialize itself from a string
 * and back again.
 *
 */
class RDFValue {
 protected:
  DataStore *resolver;

 public:
  explicit RDFValue(DataStore *resolver): resolver(resolver) {}
  RDFValue(): resolver(NULL) {}

  virtual raptor_term *GetRaptorTerm(raptor_world *world) const {
    return NULL;
  }

  // RDFValues must provide methods for serializing and unserializing.
  virtual string SerializeToString() const {
    return "";
  }

  virtual AFF4Status UnSerializeFromString(const char *data, int length) {
    return GENERIC_ERROR;
  }

  AFF4Status UnSerializeFromString(const string data) {
    return UnSerializeFromString(data.data(), data.size());
  }

  AFF4Status Set(const string data) {
    return UnSerializeFromString(data.c_str(), data.size());
  }

  virtual ~RDFValue() {}
};


// A Global Registry for RDFValue. This factory will provide the correct
// RDFValue instance based on the turtle type URN. For example xsd:integer ->
// XSDInteger().
extern ClassFactory<RDFValue> RDFValueRegistry;

template<class T>
class RDFValueRegistrar {
 public:
  explicit RDFValueRegistrar(string name) {
    // register the class factory function
    RDFValueRegistry.RegisterFactoryFunction(
        name,
        [](DataStore *resolver, const URN *urn) -> RDFValue * {
          return new T(resolver);});
  }
};


static const char* const lut = "0123456789ABCDEF";

/**
 * RDFBytes is an object which stores raw bytes. It serializes into an
 * xsd:hexBinary type.
 *
 */
class RDFBytes: public RDFValue {
 public:
  string value;

  explicit RDFBytes(string data):
      RDFBytes(data.c_str(), data.size()) {}

  RDFBytes(const char * data, unsigned int length):
      RDFValue(), value(data, length) {}

  explicit RDFBytes(DataStore *resolver): RDFValue(resolver) {}

  RDFBytes() {}

  string SerializeToString() const;
  AFF4Status UnSerializeFromString(const char *data, int length);
  raptor_term *GetRaptorTerm(raptor_world *world) const;

  bool operator==(const RDFBytes& other) const {
    return this->value == other.value;
  }

  bool operator==(const string& other) const {
    return this->value == other;
  }
};

/**
 * An XSDString is a printable string. It serializes into an xsd:string type.
 *
 */
class XSDString: public RDFBytes {
 public:
  XSDString(string data):
      RDFBytes(data.c_str(), data.size()) {}

  XSDString(const char * data):
      RDFBytes(data, strlen(data)) {}

  explicit XSDString(DataStore *resolver): RDFBytes(resolver) {}

  XSDString() {}

  string SerializeToString() const;
  AFF4Status UnSerializeFromString(const char *data, int length);
  raptor_term *GetRaptorTerm(raptor_world *world) const;
};


/**
 * A XSDInteger stores an integer. We can parse xsd:integer, xsd:int and
 * xsd:long.
 *
 */
class XSDInteger: public RDFValue {
 public:
  uint64_t value;

  explicit XSDInteger(uint64_t data):
      RDFValue(NULL), value(data) {}

  explicit XSDInteger(DataStore *resolver):
      RDFValue(resolver) {}

  XSDInteger() {}

  string SerializeToString() const;

  AFF4Status UnSerializeFromString(const char *data, int length);

  raptor_term *GetRaptorTerm(raptor_world *world) const;
};


/**
 * A XSDBoolean stores a boolean. We can parse xsd:boolean.
 *
 */
class XSDBoolean: public RDFValue {
 public:
  bool value;

  explicit XSDBoolean(bool data):
      RDFValue(NULL), value(data) {}

  explicit XSDBoolean(DataStore *resolver):
      RDFValue(resolver) {}

  XSDBoolean() {}

  string SerializeToString() const;

  AFF4Status UnSerializeFromString(const char *data, int length);

  raptor_term *GetRaptorTerm(raptor_world *world) const;
};

/**
 * Once a URN is parsed we place its components into one easy to use struct.
 *
 */
struct uri_components {
  string scheme;
  string domain;
  string fragment;
  string path;
};

/**
 * An RDFValue to store and parse a URN.
 *
 */
class URN: public XSDString {
 protected:
  string original_filename;

 public:
  /**
   * Create a new URN from a filename.
   *
   * @param filename: The filename to convert.
   * @param windows_filename: If true interpret the filename as a windows
   * filename, else it will be considered a unix filename. Currently windows and
   * unix filenames are escaped differently.
   *
   * @return a URN object.
   */
  static URN NewURNFromOSFilename(string filename, bool windows_filename,
                                  bool absolute_path = true);

  /**
   * Create a URN from filename.
   * This variant of the function automatically selects the type.
   *
   * @param filename
   * @param absolute_path: If specified we convert the filename to an absolute
   * path first.
   *
   * @return
   */
  static URN NewURNFromFilename(string filename, bool absolute_path = true);

  /**
   * Returns the current URN as a filename.
   *
   *
   * @return If this is a file:// URN, returns the filename, else "".
   */
  string ToFilename() const;

  URN(const char * data);
  URN(const string data): URN(data.c_str()) {};
  explicit URN(DataStore *resolver): URN() {};
  URN() {};

  URN Append(const string &component) const;

  raptor_term *GetRaptorTerm(raptor_world *world) const;
  uri_components Parse() const;

  // Convenience methods for Parse()
  string Scheme() const {
    return Parse().scheme;
  }

  string Path() const {
    return Parse().path;
  }

  string Domain() const {
    return Parse().domain;
  }

  /**
   * returns the path of the URN relative to ourselves.
   *
   * If the urn contains us as a common prefix, we remove that and return a
   * relative path. Otherwise we return the complete urn as an absolution path.
   *
   * @param urn: The urn to check.
   *
   * @return A string representing the path.
   */
  string RelativePath(const URN urn) const;

  AFF4Status Set(const URN data) {
    value = data.SerializeToString();
    return STATUS_OK;
  }
};


#endif  // SRC_RDF_H_
