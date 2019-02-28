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


#ifndef     SRC_LIBAFF4_H_
#define     SRC_LIBAFF4_H_

#include "aff4/config.h"

#include "aff4/rdf.h"
#include "aff4/aff4_io.h"
#include "aff4/aff4_image.h"
#include "aff4/aff4_directory.h"
#include "aff4/aff4_map.h"
#include "aff4/data_store.h"
#include "aff4/zip.h"
#include "aff4/lexicon.h"


namespace aff4 {

/* Utility functions. */

/**
 * Convert from a child URN to the zip member name.
 *
 * The AFF4 ZipFile stores AFF4 objects (with fully qualified URNs) in zip
 * archives. The zip members name is based on the object's URN with the
 * following rules:

 1. If the object's URN is an extension of the volume's URN, the member's name
 will be the relative name. So for example:

 Object: aff4://9db79393-53fa-4147-b823-5c3e1d37544d/Foobar.txt
 Volume: aff4://9db79393-53fa-4147-b823-5c3e1d37544d

 Member name: Foobar.txt

 2. All charaters outside the range [a-zA-Z0-9_] shall be escaped according to
 their hex encoding.

 * @param name
 *
 * @return The member name in the zip archive.
 */
std::string member_name_for_urn(const URN member, const URN base_urn,
                                bool slash_ok = false);

URN urn_from_member_name(const std::string& member, const URN base_urn);

// Accepts both / and \ separators.
 std::vector<std::string> break_path_into_components(std::string path);
 std::string join(const std::vector<std::string>& v, char c);
 std::string escape_component(std::string filename);

extern "C" {
    const char* AFF4_version();
}


std::string aff4_sprintf(std::string fmt, ...);

const char LOGGER[] = "aff4";

} // namespace aff4


#endif    // SRC_LIBAFF4_H_
