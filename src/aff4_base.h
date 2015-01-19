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

#ifndef AFF4_BASE_H
#define AFF4_BASE_H
// Decleations for basic AFF4 types.

#include "lexicon.h"
#include "aff4_errors.h"
#include "rdf.h"
#include "data_store.h"

template<typename T>
RDFValue *fCreate() {
  return new T();
};

struct AFF4Schema {
  string classname;
  RDFValue* (*constructor)();
};

/**
 * The base class for all AFF4 objects. It is not usually possible to
 * instantiate a plain AFF4 Object since it does not really do anything.
 *
 */
class AFF4Object {

 protected:
  string name = "AFF4Object";

 public:
  /// Each AFF4 object is addressable by its URN.
  URN urn;

  AFF4Object();

  // By defining a virtual destructor this allows the destructor of derived
  // objects to be called when deleting a pointer to a base object.
  virtual ~AFF4Object() {};

  /**
   * Load this AFF4 object from the URN provided.
   *
   *
   * @return STATUS_OK if the object was properly loaded.
   */
  virtual AFF4Status LoadFromURN(const string &mode) {
    return NOT_IMPLEMENTED;
  };

  AFF4Status Flush();
};


/**
 * A registry for AFF4 objects. This is used to instantiate the correct
 * AFF4Object at a specific URN.
 *
 */
extern ClassFactory<AFF4Object> AFF4ObjectRegistry;


template<class T>
class AFF4Registrar {
 public:
  AFF4Registrar(string name) {
    AFF4ObjectRegistry.RegisterFactoryFunction(
        name,
        [](void) -> T *{ return new T(); });
  };
};


template<typename T>
unique_ptr<T> AFF4FactoryOpen(const URN &urn, const string &mode = "r") {
  URN aff4_type(AFF4_TYPE);
  URN type_urn;
  const uri_components components = urn.Parse();

  // Try to instantiate the handler based on the URN alone.
  unique_ptr<AFF4Object> obj = AFF4ObjectRegistry.CreateInstance(
      components.scheme);

  // Ask the oracle for the correct type.
  if (!obj) {
    if (oracle.Get(urn, aff4_type, type_urn) != STATUS_OK) {
      return NULL;
    };

    obj = AFF4ObjectRegistry.CreateInstance(type_urn.value);
  };

  if (!obj)
    return NULL;

  obj->urn = urn;
  if(obj->LoadFromURN(mode) != STATUS_OK)
    return NULL;

  return unique_ptr<T>(dynamic_cast<T *>(obj.release()));
};


#endif // AFF4_BASE_H
