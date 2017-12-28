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

#include <cstring>
#include "lexicon.h"
#include "libaff4.h"
#include "libaff4-c.h"

// The application resolver
static aff4::MemoryDataStore* resolver = nullptr;
// The map of handles to AFF4 Map instances.
static std::unordered_map<int, aff4::URN> handles;
// The next handle.
static int nextHandle;

extern "C" {

void AFF4_init() {
    resolver = new aff4::MemoryDataStore();
}

int AFF4_open(char* filename) {
        if(resolver == nullptr){
                AFF4_init();
        }
        int handle = nextHandle++;
        aff4::URN urn = aff4::URN::NewURNFromFilename(filename);
        aff4::AFF4ScopedPtr<aff4::ZipFile> zip = aff4::ZipFile::NewZipFile(
            resolver, urn);
        if (!zip) {
            errno = ENOENT;
            return -1;
        }
        // Attempt AFF4 Standard, and if not, fallback to AFF4 Evimetry Legacy format.
        std::shared_ptr<aff4::RDFValue> value = std::shared_ptr<aff4::RDFValue>(
            new aff4::URN(aff4::AFF4_IMAGE_TYPE));
        std::unordered_set<aff4::URN> images = resolver->Query(
            aff4::URN(aff4::AFF4_TYPE), value);
        if (images.empty()) {
            value = std::shared_ptr<aff4::RDFValue>(
                new aff4::URN(aff4::AFF4_LEGACY_IMAGE_TYPE));
            images = resolver->Query(aff4::URN(aff4::AFF4_TYPE), value);
            if (images.empty()) {
                resolver->Close(zip);
                errno = ENOENT;
                return -1;
            }
        }
        aff4::AFF4ScopedPtr<aff4::AFF4Map> map = resolver->AFF4FactoryOpen<
            aff4::AFF4Map>(*(images.begin()));
        if (map.get() == nullptr) {
            resolver->Close(zip);
            errno = ENOENT;
            return -1;
        }
        handles[handle] = *(images.begin());
        return handle;
}

uint64_t AFF4_object_size(int handle) {
        if(resolver == nullptr){
            AFF4_init();
        }
        aff4::URN urn = handles[handle];
        auto map = resolver->AFF4FactoryOpen<aff4::AFF4Map>(urn);
        if (map.get() != nullptr) {
            return map->Size();
        }
        return 0;
}

int AFF4_read(int handle, uint64_t offset, void* buffer, int length) {
        if(resolver == nullptr){
                AFF4_init();
        }
        aff4::URN urn = handles[handle];
        auto map = resolver->AFF4FactoryOpen<aff4::AFF4Map>(urn);
        int read = 0;
        if (map.get() != nullptr) {
            map->Seek(offset, SEEK_SET);
            std::string result = map->Read(length);
            read = result.length();
            std::memcpy(buffer, result.data(), read);
        } else {
            errno = ENOENT;
        }
        return read;
}

int AFF4_close(int handle) {
        if(resolver == nullptr){
            AFF4_init();
        }
        aff4::URN urn = handles[handle];
        auto map = resolver->AFF4FactoryOpen<aff4::AFF4Map>(urn);
        if (map.get() != nullptr) {
            resolver->Close(map);
            handles.erase(handle);
        }
        return 0;
}

}
