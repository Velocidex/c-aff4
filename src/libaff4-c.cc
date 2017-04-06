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

#include "libaff4.h"
#include "libaff4-c.h"

// The application resolver
static MemoryDataStore* resolver = nullptr;
// The map of handles to AFF4 Map instances.
static std::unordered_map<int, URN> handles;
// The next handle.
static int nextHandle;

extern "C" {

//void AFF4_init (void) __attribute__((constructor));

void AFF4_init() {
	// Set GLOG to quiet.
	google::InitGoogleLogging("libaff4");
	google::LogToStderr();
	google::SetStderrLogging(google::GLOG_ERROR);

	nextHandle = 1;
	resolver = new MemoryDataStore();
}

int AFF4_open(char* filename) {
	if(resolver == nullptr){
		AFF4_init();
	}
	int handle = nextHandle++;
	URN urn = URN::NewURNFromFilename(filename);
	AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(resolver, urn);
	if (!zip) {
		errno = ENOENT;
		return -1;
	}
	// Attempt AFF4 Standard, and if not, fallback to AFF4 Evimetry Legacy format.
	std::shared_ptr<RDFValue> value = std::shared_ptr<RDFValue>(new URN(AFF4_IMAGE_TYPE));
	std::unordered_set<URN> images = resolver->Query(URN(AFF4_TYPE), value);
	if (images.empty()) {
		value = std::shared_ptr<RDFValue>(new URN(AFF4_LEGACY_IMAGE_TYPE));
		images = resolver->Query(URN(AFF4_TYPE), value);
		if (images.empty()) {
			resolver->Close(zip);
			errno = ENOENT;
			return -1;
		}
	}
	AFF4ScopedPtr<AFF4Map> map = resolver->AFF4FactoryOpen<AFF4Map>(*(images.begin()));
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
	URN urn = handles[handle];
	AFF4ScopedPtr<AFF4Map> map = resolver->AFF4FactoryOpen<AFF4Map>(urn);
	if (map.get() != nullptr) {
		return map->Size();
	}
	return 0;
}

int AFF4_read(int handle, uint64_t offset, void* buffer, int length) {
	if(resolver == nullptr){
		AFF4_init();
	}
	URN urn = handles[handle];
	AFF4ScopedPtr<AFF4Map> map = resolver->AFF4FactoryOpen<AFF4Map>(urn);
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
	URN urn = handles[handle];
	AFF4ScopedPtr<AFF4Map> map = resolver->AFF4FactoryOpen<AFF4Map>(urn);
	if (map.get() != nullptr) {
		resolver->Close(map);
		handles.erase(handle);
	}
	return 0;
}

}
