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
 public:
  /// Each AFF4 object is addressable by its URN.
  URN urn;

  DataStore *resolver;                  /**< All AFF4Objects have a resolver
                                         * which they use to access information
                                         * about the AFF4 universe. */

  AFF4Object(DataStore *resolver);

  // By defining a virtual destructor this allows the destructor of derived
  // objects to be called when deleting a pointer to a base object.
  virtual ~AFF4Object() {};

  /**
   * Load this AFF4 object from the URN provided.
   *
   *
   * @return STATUS_OK if the object was properly loaded.
   */
  virtual AFF4Status LoadFromURN() {
    return NOT_IMPLEMENTED;
  };

  /**
   * Prepares an object for re-use. Since AFF4Objects can be cached, we need a
   * way to reset the object to a consistent state when returning it from the
   * cache. The AFF4FactoryOpen() function will call this method before
   * returning it to the caller in order to reset the object into a consistent
   * state.
   *
   *
   * @return STATUS_OK
   */
  virtual AFF4Status Prepare() {
    return STATUS_OK;
  };

  virtual AFF4Status Flush();
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
        [](DataStore *resolver) -> T *{ return new T(resolver); });
  };
};


template<typename T>
T *AFF4FactoryOpen(DataStore *resolver, const URN &urn) {
  // Search the object cache first.
  auto it = resolver->ObjectCache.find(urn.value);
  if (it != resolver->ObjectCache.end()) {
    it->second->Prepare();
    return dynamic_cast<T *>(it->second.get());
  };

  URN aff4_type(AFF4_TYPE);
  URN type_urn;
  unique_ptr<AFF4Object> obj;

  // Check if there is a resolver triple for it.
  if (resolver->Get(urn, aff4_type, type_urn) == STATUS_OK) {
    obj = AFF4ObjectRegistry.CreateInstance(type_urn.value, resolver);

  } else {
    const uri_components components = urn.Parse();

    // Try to instantiate the handler based on the URN scheme alone.
    obj = AFF4ObjectRegistry.CreateInstance(components.scheme, resolver);
  };

  // Failed to find the object.
  if (!obj)
    return NULL;

  // Have the object load and initialize itself.
  obj->urn = urn;
  if(obj->LoadFromURN() != STATUS_OK) {
    DEBUG_OBJECT("Failed to load %s as %s",
                 urn.value.c_str(), type_urn.value.c_str());
    return NULL;
  };

  // Cache the object for next time.
  T *result = dynamic_cast<T *>(obj.get());
  resolver->ObjectCache[urn.value] = std::move(obj);

  return result;
};


#endif // AFF4_BASE_H
