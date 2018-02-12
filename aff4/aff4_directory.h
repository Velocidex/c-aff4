/*
Copyright 2015 Google Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use
this file except in compliance with the License.  You may obtain a copy of the
License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.  See the License for the
specific language governing permissions and limitations under the License.
*/

// This file defines the Directory AFF4 volume implementation.

#ifndef     SRC_AFF4_DIRECTORY_H_
#define     SRC_AFF4_DIRECTORY_H_

#include "aff4/config.h"

#include "aff4/aff4_errors.h"
#include "aff4/aff4_io.h"
#include "aff4/aff4_file.h"
#include "aff4/data_store.h"
#include <string.h>


namespace aff4 {

class AFF4Directory: public AFF4Volume {
  public:
    // Where we are stored.
    URN storage;
    std::string root_path;

    explicit AFF4Directory(DataStore* resolver): AFF4Volume(resolver) {}
    AFF4Directory(DataStore* resolver, URN urn):
        AFF4Volume(resolver, urn) {}

    /**
     * Creates a new AFF4Directory object.
     *
     * @param root_urn: The URN of a root directory.
     *
     * @return A new AFF4Directory reference.
     */
    static AFF4ScopedPtr<AFF4Directory> NewAFF4Directory(
        DataStore* resolver, URN root_urn);

    // Generic volume interface. NOTE: The AFF4Directory can only contain
    // FileBackedObject instances so this is what will be returned here.
    virtual AFF4ScopedPtr<AFF4Stream> CreateMember(URN child);

    // Load the AFF4Directory from its URN and the information in the oracle.
    virtual AFF4Status LoadFromURN();

    // Update the information.turtle file.
    virtual AFF4Status Flush();

    // Some handy static methods.
    static bool IsDirectory(const URN& urn, bool must_exist=false);
    static bool IsDirectory(const std::string& filename, bool must_exist=false);
    static AFF4Status RemoveDirectory(DataStore *resolver, const std::string& root_path);
    static AFF4Status MkDir(DataStore *resolver, const std::string& path);
};

void aff4_directory_init();

} // namespace aff4

#endif    // SRC_AFF4_DIRECTORY_H_
