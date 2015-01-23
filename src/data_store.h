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

/**
 * @file   data_store.h
 * @author scudette <scudette@google.com>
 * @date   Fri Jan 23 12:11:05 2015
 *
 * @brief This file defines the AFF4 data store abstraction.
 *
 * AFF4 relies on the data store to maintain relational information about the
 * AFF4 universe. This relation information is used to reconstruct objects which
 * have been previously stored in this data store.
 *
 * Note: In this implementation the data store caches all AFF4 objects which
 * have been produced and flushes them when the DataStore::Flush() method is
 * called. The Flush() method is also called during object destruction.

 * This essentially defines a transaction, for example, to open an AFF4 Zip
 * volume, add a new image to it and close it:

~~~~~~~~~~~~~{.c}
  // This essentially starts a transaction in the Volume
  unique_ptr<DataStore> resolver(new MemoryDataStore());

  // This will open and reparse the zip file, populating the resolver.
  AFF4Volume *zip = ZipFile::NewZipFile(resolver.get(), "file.zip");

  // This creates a new image with URN "image.dd" inside the zip file's URN.
  AFF4Image *image = AFF4Image::NewAFF4Image(
       resolver.get(), "image.dd", zip->urn);

  // Write something on the image.
  image->sprintf("Hello world!");

  // This will flush all images, close the zip file etc. This method is also
  // automatically called when the resolver is destructed so it is unnecessary
  // here.
  resolver->Flush();
~~~~~~~~~~~~~

 */

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


// AFF4_Attributes are a collection of RDFValue objects, keyed by attributes.
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

  virtual AFF4Status Get(const URN &urn, const URN &attribute,
                         RDFValue &value) = 0;

  virtual void Set(const URN &urn, const URN &attribute,
                   unique_ptr<RDFValue> value) = 0;

  virtual AFF4Status DeleteSubject(const URN &urn) = 0;

  // Dump ourselves to a yaml file.
  virtual AFF4Status DumpToYaml(AFF4Stream &output) = 0;
  virtual AFF4Status DumpToTurtle(AFF4Stream &output) = 0;

  virtual AFF4Status LoadFromYaml(AFF4Stream &output) = 0;
  virtual AFF4Status LoadFromTurtle(AFF4Stream &output) = 0;

  /**
   * Clear all data.
   *
   *
   * @return Status
   */
  virtual AFF4Status Clear() = 0;


  /**
   * Flush all objects cached in the data store.
   *
   *
   * @return Status.
   */
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
  /**
   * Set the RDFValue in the data store. Note that the data store will retain
   * ownership of the value, and therefore callers may not use it after this
   * call.
   *
   * @param urn: The subject to set the attribute for.
   * @param attribute: The attribute to set.
   * @param value: The value.
   */
  virtual void Set(const URN &urn, const URN &attribute, RDFValue *value);
  virtual void Set(const URN &urn, const URN &attribute,
                   unique_ptr<RDFValue> value);

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
