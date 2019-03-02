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
    std::string root_path;

    explicit AFF4Directory(DataStore* resolver): AFF4Volume(resolver) {}

    static AFF4Status NewAFF4Directory(
        DataStore *resolver, std::string dirname, bool truncate,
        AFF4Flusher<AFF4Volume> &result);

    static AFF4Status NewAFF4Directory(
        DataStore *resolver, std::string dirname, bool truncate,
        AFF4Flusher<AFF4Directory> &result);

    static AFF4Status OpenAFF4Directory(
        DataStore *resolver, std::string dirname, AFF4Flusher<AFF4Directory> &result);

    AFF4Status OpenMemberStream(
        URN child, AFF4Flusher<AFF4Stream> &result) override;

    AFF4Status CreateMemberStream(
        URN child, AFF4Flusher<AFF4Stream> &result) override;

    // Update the information.turtle file.
    AFF4Status Flush() override;

    // Some handy static methods.
    static bool IsDirectory(const URN& urn, bool must_exist=false);
    static bool IsDirectory(const std::string& filename, bool must_exist=false);
    static AFF4Status RemoveDirectory(DataStore *resolver, const std::string& root_path);
    static AFF4Status MkDir(DataStore *resolver, const std::string& path);
};

} // namespace aff4

#endif    // SRC_AFF4_DIRECTORY_H_
