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

#ifndef SRC_AFF4_BASE_H_
#define SRC_AFF4_BASE_H_

#include "config.h"

#include <iostream>


#ifdef _WIN32
#define PATH_SEP '\\'
#define PATH_SEP_STR "\\"
#else
#define PATH_SEP '/'
#define PATH_SEP_STR "/"
#endif


/**
 * @file
 * @author scudette <scudette@google.com>
 * @date   Fri Jan 23 10:44:31 2015
 *
 * @brief  Decleations for the base AFF4 types and the AFF4 Factory.
 *
 *
 */

#if defined(_WIN32)
#include "windows.h"

#ifndef INTERNET_MAX_URL_LENGTH
#define INTERNET_MAX_URL_LENGTH 2056
#endif

#endif

// Windows defines this macro which interfers with glog's version.
#undef ERROR

#include "lexicon.h"
#include "aff4_errors.h"
#include "rdf.h"
#include <glog/logging.h>


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
  protected:
    bool _dirty = false;                  /**< true if the object was modified. */

  public:
    URN urn;                              /**< Each AFF4 object is addressable by
                                         * its URN. */

    DataStore* resolver;                  /**< All AFF4Objects have a resolver
                                         * which they use to access information
                                         * about the AFF4 universe. */

    AFF4Object(DataStore* resolver);// : resolver(resolver){}

    AFF4Object(DataStore* resolver, URN new_urn): AFF4Object(resolver) {
        urn = new_urn;
    }

    // Return this object to the resolver.
    void Return();

    // By defining a virtual destructor this allows the destructor of derived
    // objects to be called when deleting a pointer to a base object.
    virtual ~AFF4Object() {}

    /**
     * Load this AFF4 object from the URN provided.
     *
     *
     * @return STATUS_OK if the object was properly loaded.
     */
    virtual AFF4Status LoadFromURN() {
        return NOT_IMPLEMENTED;
    }

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
    }

    /**
     * Flush the object state to the resolver data store.
     * This is the reverse of LoadFromURN().
     *
     * @return
     */
    virtual AFF4Status Flush();

    /**
     * Has the object been modified in any way?
     *
     *
     * @return true if the object is dirty.
     */
    virtual bool IsDirty() {
        return _dirty;
    }

    /**
     * Mark the object as dirty. Note that the only way the object can become
     * non-dirty is through a flush.
     */
    virtual void MarkDirty() {
        _dirty = true;
    }
};


/**
 * A registry for AFF4 objects. This is used to instantiate the correct
 * AFF4Object at a specific URN.
 *
 */
ClassFactory<AFF4Object>* GetAFF4ClassFactory();


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
    std::string name;
    explicit AFF4Registrar(std::string name) {
        this->name = name;

        GetAFF4ClassFactory()->RegisterFactoryFunction(
            name,
        [](DataStore *resolver, const URN *urn) -> T * {
            UNUSED(urn);
            return new T(resolver);
        });
    }
};

// In the AFF4 library all offsets are 64 bits - even on windows!
typedef int64_t aff4_off_t;

#endif  // SRC_AFF4_BASE_H_
