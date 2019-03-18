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

#include "aff4/config.h"

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

#include "aff4/lexicon.h"
#include "aff4/aff4_errors.h"
#include "aff4/rdf.h"

namespace aff4 {


/**
 * The base class for all AFF4 objects. It is not usually possible to
 * instantiate a plain AFF4 Object since it does not really do anything.

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

    // By defining a virtual destructor this allows the destructor of derived
    // objects to be called when deleting a pointer to a base object.
    virtual ~AFF4Object() {}

    virtual AFF4Status Flush();

    /**
     * Has the object been modified in any way?
     *
     *
     * @return true if the object is dirty.
     */
    virtual bool IsDirty() const {
        return _dirty;
    }

    /**
     * Mark the object as dirty. Note that the only way the object can become
     * non-dirty is through a flush.
     */
    virtual void MarkDirty() {
        _dirty = true;
    }

 private:
    // AFF4Objects are not trivially copyable.
    AFF4Object(const AFF4Object&) = delete;
    AFF4Object& operator=(const AFF4Object&) = delete;
};

// In the AFF4 library all offsets are 64 bits - even on windows!
typedef int64_t aff4_off_t;

} // namespace aff4

#endif  // SRC_AFF4_BASE_H_
