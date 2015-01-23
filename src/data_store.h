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

#ifndef  _AFF4_DATA_STORE_H_
#define  _AFF4_DATA_STORE_H_

// This file defines the AFF4 data store abstraction.
#include <unordered_map>
#include <string>
#include <memory>
#include <fstream>
#include "aff4_utils.h"
#include <string.h>
#include "rdf.h"

using std::string;
using std::unique_ptr;
using std::unordered_map;
using std::ofstream;
using std::ifstream;


// Forward declerations for basic AFF4 types.
class AFF4Object;
class AFF4Stream;
class AFF4Volume;

enum DataStoreObjectWireType {
  STRING,
  INTEGER
};

/* These are the objects which are stored in the data store */
class DataStoreObject {
 public:
  // The data store can only store the following primitive types.
  int int_data = 0;
  string string_data;
  DataStoreObjectWireType wire_type = STRING;

  DataStoreObject(int data): int_data(data), wire_type(INTEGER) {};
  DataStoreObject(string data): string_data(data), wire_type(STRING) {};
  DataStoreObject(){};
};

// AFF4_Attributes are a collection of data store objects, keyed by attributes.
typedef unordered_map<string, unique_ptr<RDFValue> > AFF4_Attributes;


/** The abstract data store.

    Data stores know how to serialize RDF statements of the type:

    subject predicate value

    Where both subject and predicate are a URN into the AFF4 space, and value is
    a serialized RDFValue.
 */
class DataStore {
 public:
  virtual void Set(const URN &urn, const URN &attribute,
                   RDFValue *value) = 0;

  virtual AFF4Status Get(const URN &urn, const URN &attribute, RDFValue &value) = 0;
  virtual void Set(const URN &urn, const URN &attribute, unique_ptr<RDFValue> value) = 0;

  virtual AFF4Status DeleteSubject(const URN &urn) = 0;

  // Dump ourselves to a yaml file.
  virtual AFF4Status DumpToYaml(AFF4Stream &output) = 0;
  virtual AFF4Status DumpToTurtle(AFF4Stream &output) = 0;

  virtual AFF4Status LoadFromYaml(AFF4Stream &output) = 0;
  virtual AFF4Status LoadFromTurtle(AFF4Stream &output) = 0;

  // Clear all data.
  virtual AFF4Status Clear() = 0;
  virtual AFF4Status Flush() = 0;

  /**
   * An object cache for objects created via the AFF4FactoryOpen()
   * interface. Note that the cache owns all objects at all times.
   *
   */
  unordered_map<string, unique_ptr<AFF4Object> > ObjectCache;

  virtual ~DataStore();

  /**
   * Prints out the contents of the resolver to STDOUT. Used for debugging.
   *
   */
  void Dump();
};


/** A purely in memory data store.

    This data store can be initialized and persisted into a Yaml file.
*/
class MemoryDataStore: public DataStore {

 private:
  // Store a collection of AFF4_Attributes at each URN.
  unordered_map<string, AFF4_Attributes> store;

 public:
  // Set the RDFValue in the data store. Note that the data store will retain
  // ownership of the value, and therefore callers may not use it after this
  // call.
  virtual void Set(const URN &urn, const URN &attribute, RDFValue *value);
  virtual void Set(const URN &urn, const URN &attribute, unique_ptr<RDFValue> value);

  AFF4Status Get(const URN &urn, const URN &attribute, RDFValue &value);

  virtual AFF4Status DeleteSubject(const URN &urn);

  virtual AFF4Status DumpToYaml(AFF4Stream &output);
  virtual AFF4Status DumpToTurtle(AFF4Stream &output);

  virtual AFF4Status LoadFromYaml(AFF4Stream &output);
  virtual AFF4Status LoadFromTurtle(AFF4Stream &output);

  virtual AFF4Status Clear();
  virtual AFF4Status Flush();

  virtual ~MemoryDataStore();
};

#endif //  _AFF4_DATA_STORE_H_
