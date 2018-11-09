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
#include "aff4/lexicon.h"
#include "aff4/libaff4.h"
#include "aff4/libaff4-c.h"

// The application resolver
static aff4::MemoryDataStore* resolver = nullptr;


// The next handle.
static int nextHandle = 0;

std::unordered_map<int, aff4::URN> *get_handles() {
    // The map of handles to AFF4 Map instances.
    static auto* handles = new std::unordered_map<int, aff4::URN>();
    return handles;
}

bool GetHandle(int handle, aff4::URN *urn) {
    auto handles = get_handles();
    const auto& it = handles->find(handle);
    if (it == handles->end())
        return false;

    *urn = it->second;

    return true;
}


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
    const aff4::URN type(aff4::AFF4_IMAGE_TYPE);
    std::unordered_set<aff4::URN> images = resolver->Query(aff4::AFF4_TYPE, &type);

    if (images.empty()) {
        const aff4::URN legacy_type(aff4::AFF4_LEGACY_IMAGE_TYPE);
        images = resolver->Query(aff4::URN(aff4::AFF4_TYPE), &legacy_type);
        if (images.empty()) {
            resolver->Close(zip);
            errno = ENOENT;
            return -1;
        }
    }

    // Sort URNs so that we have some sort of determinism
    std::vector<aff4::URN> sorted_images{images.begin(), images.end()};
    std::sort(sorted_images.begin(), sorted_images.end());

    // Make sure we only load an image that is stored in the filename provided
    for (const auto & image : sorted_images) {
        if (!resolver->HasURNWithAttributeAndValue(image, aff4::AFF4_STORED, zip->urn)) {
            continue;
        }

        if(!resolver->AFF4FactoryOpen<aff4::AFF4StdImage>(image)) {
            continue;
        }

        (*get_handles())[handle] = image;
        return handle;
    }

    resolver->Close(zip);
    errno = ENOENT;
    return -1;
}

uint64_t AFF4_object_size(int handle) {
    if(resolver == nullptr){
        AFF4_init();
    }

    aff4::URN urn;
    if (GetHandle(handle, &urn)) {
        auto stream = resolver->AFF4FactoryOpen<aff4::AFF4Stream>(urn);
        if (stream.get() != nullptr) {
            return stream->Size();
        }
    }
    return 0;
}

int AFF4_read(int handle, uint64_t offset, void* buffer, int length) {
    if(resolver == nullptr){
        AFF4_init();
    }
    aff4::URN urn;

    if (!GetHandle(handle, &urn)) return -1;

    auto stream = resolver->AFF4FactoryOpen<aff4::AFF4Stream>(urn);
    int read = 0;
    if (stream.get() != nullptr) {
        stream->Seek(offset, SEEK_SET);
        std::string result = stream->Read(length);
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
    aff4::URN urn;
    if (GetHandle(handle, &urn)) {
        auto obj = resolver->AFF4FactoryOpen<aff4::AFF4Object>(urn);
        if (obj.get() != nullptr) {
            resolver->Close(obj);
            get_handles()->erase(handle);
        }
    }
    return 0;
}

}
