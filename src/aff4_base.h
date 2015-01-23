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

/**
 * @file
 * @author scudette <scudette@google.com>
 * @date   Fri Jan 23 10:44:31 2015
 *
 * @brief  Decleations for the base AFF4 types and the AFF4 Factory.
 *
 *
 */

#include "lexicon.h"
#include "aff4_errors.h"
#include "rdf.h"
#include "data_store.h"

/**
 * The base class for all AFF4 objects. It is not usually possible to
 * instantiate a plain AFF4 Object since it does not really do anything.
 *

 AFF4Objects must implement the following protocol:

1. The object must be instantiatable using the resolver alone. This is termed
   the empty object. Empty objects receive a random URN so they are always valid
   and unique in the AFF4 space.

2. Each object must be completely serializable into the resolver data
   store. This means that the state of the object can be fully reconstructed
   from the data store at any time. Objects must implement the LoadFromURN()
   method to reconstruct the state of the object from the data store (from its
   current URN). Similarly the Flush() method should write the current object
   state to the data store. Note that technically there is no restriction of the
   lexicon of RDF pairs that can be used to serialize an object. Standard AFF4
   objects define their lexicon in "lexicon.h".

3. AFF4Objects are cached and reused in different contexts. The object must
   implement the Prepare() method to restore itself to a known fixed state prior
   to being given to a new user. Since AFF4Objects are always created by the
   AFF4Factory, the factory will prepare the object prior to returning it to
   users.

4. An AFF4Object must call Flush on all objects which depend on it to ensure the
   proper order of flushing.

 * @param resolver: Each AFF4Object must be instantiated with an instance of the
 *        resolver. The resolver is assumed to outlive all AFF4Object instances
 *        which reference it.
 */
class AFF4Object {
 public:
  URN urn;                              /**< Each AFF4 object is addressable by
                                         * its URN. */

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

  /**
   * Flush the object state to the resolver data store.
   * This is the reverse of LoadFromURN().
   *
   * @return
   */
  virtual AFF4Status Flush();
};


/**
 * A registry for AFF4 objects. This is used to instantiate the correct
 * AFF4Object at a specific URN.
 *
 */
ClassFactory<AFF4Object> *GetAFF4ClassFactory();


/**
 * A registration class for new AFF4 objects.
 *
 * Registration occurs through static instances of the AFF4Registrar class. For
 * example the following registers the ZipFile class to handle the AFF4_ZIP_TYPE
 * type:

~~~~~~~~~~~{.c}
static AFF4Registrar<ZipFile> r1(AFF4_ZIP_TYPE);
~~~~~~~~~~~

 *
 * @param name
 */
template<class T>
class AFF4Registrar {
 public:
  AFF4Registrar(string name) {
    GetAFF4ClassFactory()->RegisterFactoryFunction(
        name,
        [](DataStore *resolver) -> T *{ return new T(resolver); });
  };
};


/**
 This is the main entry point into the AFF4 library. Callers use this factory
 method to instantiate an AFF4Object of a particular type based on its
 URN. The factory is passed a resolver which contains the AFF4 RDF metadata
 about the subset of the AFF4 universe we are dealing with.

 The object returned is of the type specified in the resolver (or its base
 type). Callers to the factory must declare their expected types in the template
 arg. If the object is not of the required type, the factory will not
 instantiate it.

 Note that all objects instantiated by the factory are owned by the factory at
 all times. Callers just receive a reference to the object. This allows the
 resolver to maintain a cache of objects and reuse them. Typically, therefore,
 callers may not hold the returned objects for long periods of time. Instead,
 callers should record the URN and use it to retrieve the object in future.

 When the resolver is destroyed, the objects cached by it are flushed. Therefore
 callers may use the lifetime of the resolver as a transaction for created AFF4
 objects.

 The following is an example of how to create an AFF4Image instance and write to
 it:

~~~~~~~~~~~{.c}
void test_ZipFileCreate() {
  unique_ptr<DataStore> resolver(new MemoryDataStore());
  AFF4Stream* file = AFF4FactoryOpen<AFF4Stream>(resolver.get(), "test.zip");

  // The backing file is given to the zip.
  AFF4Volume* zip = ZipFile::NewZipFile(resolver.get(), file->urn);

  AFF4Stream *segment = zip->CreateMember("Foobar.txt");
  segment->Write("I am a segment!");
};
~~~~~~~~~~~

 * @param resolver: The resolver to use.
 * @param urn: The URN to instantiate.
 *
 * @return A instance of T or NULL if an object of this type is not known at the
 *         specified URN. Note that callers do not own the object and must not
 *         hold persistent references to it.
 */
template<typename T>
T *AFF4FactoryOpen(DataStore *resolver, const URN &urn) {
  // Search the object cache first.
  auto it = resolver->ObjectCache.find(urn.value);
  if (it != resolver->ObjectCache.end()) {
    it->second->Prepare();
    return dynamic_cast<T *>(it->second.get());
  };

  URN type_urn;
  unique_ptr<AFF4Object> obj;

  // Check if there is a resolver triple for it.
  if (resolver->Get(urn, AFF4_TYPE, type_urn) == STATUS_OK) {
    obj = GetAFF4ClassFactory()->CreateInstance(type_urn.value, resolver);

  } else {
    const uri_components components = urn.Parse();

    // Try to instantiate the handler based on the URN scheme alone.
    obj = GetAFF4ClassFactory()->CreateInstance(components.scheme, resolver);
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
