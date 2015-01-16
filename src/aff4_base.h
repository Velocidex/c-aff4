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
    All AFF4 objects extend this basic object.

    AFF4Objects present an external API for users. There are two main ways to
    instantiate an AFF4Object:

    1) To create a new object, one uses the static Factory function defined in
       the AFF4 public interface. For example:

       static unique_ptr<ZipFile> NewZipFile(unique_ptr<AFF4Stream> stream);

       This will return a new instance of the AFF4Object. When the object is
       deleted, it will be flushed to the AFF4 resolver.

    2) Similarly, to open an existing object, the static factory function can be
       used. e.g.:

       static unique_ptr<ZipFile> OpenZipFile(URN urn);

       Typically only the URN is required as a parameter. Note that if the
       object stored at the specified URN is not of the required type, this
       method will fail and return NULL.

    Internally all AFF4 objects must be able to be recreated exactly from the
    AFF4 resolver.  Therefore the following common pattern is followed:

       static unique_ptr<XXXX> NewXXXX(arg1, arg2, arg3) {
           this->Set(predicate1, arg1);
           this->Set(predicate2, arg2);
           this->Set(predicate3, arg2);

           // instantiate the object.
           XXXX(arg1, arg2, arg3);

           // Now, when the object is destroyed the predicates set above will
           // be flushed to storage.
       };

       static unique_ptr<XXXX> OpenXXXX(URN urn) {
          InitAFF4Attributes();  // Load all attributes from the resolver.

          arg1 = this->Get(predicate1);
          arg2 = this->Get(predicate2);
          arg3 = this->Get(predicate3);

          return unique_ptr<XXXX>(new XXX(arg1, arg2, arg3));
       };

 **/
class AFF4Object {

 protected:
  // AFF4 objects store attributes.
  AFF4_Attributes attributes;

  AFF4Schema schema[1] = {
    {"URN", &fCreate<URN>}
  };

  string name = "AFF4Object";

 public:
  URN urn;

  // AFF4 objects are created using the following pattern:
  // obj = AFF4_FACTORY.Create(type)
  // obj.Set(attribute1, value1)
  // obj.Set(attribute2, value2)
  // if (!obj.finish()) {
  //     Failed to create object.
  // }
  AFF4Object(); // Used by the factory for generic
                                    // instantiation.

  // By defining a virtual destructor this allows the destructor of derived
  // objects to be called when deleting a pointer to a base object.
  virtual ~AFF4Object() {};

  AFF4Status Flush();
};

#endif // AFF4_BASE_H
