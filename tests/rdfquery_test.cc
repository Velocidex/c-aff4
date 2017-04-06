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
#include <gtest/gtest.h>
#include <libaff4.h>
#include <unistd.h>
#include <glog/logging.h>

TEST(AFF4ImageRDFQuery, Sample1URN) {
	std::string filename = "samples/Base-Linear.aff4";

	MemoryDataStore resolver;
	// This will open the container.
	URN urn = URN::NewURNFromFilename(filename);
	AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, urn);

	std::shared_ptr<RDFValue> value = std::shared_ptr<RDFValue>(new URN(AFF4_IMAGE_TYPE));
	std::unordered_set<URN> images = resolver.Query(URN(AFF4_TYPE), value);
	ASSERT_EQ(1, images.size());
	for(URN u : images){
		//std::cout << u.SerializeToString() << std::endl;
		ASSERT_EQ(std::string("aff4://08b52fb6-fbae-45f3-967e-03502cefaf92"), u.SerializeToString());
	}
}

TEST(AFF4ImageRDFQuery, Sample2URN) {
	std::string filename = "samples/Base-Allocated.aff4";

	MemoryDataStore resolver;
	// This will open the container.
	URN urn = URN::NewURNFromFilename(filename);
	AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, urn);

	std::shared_ptr<RDFValue> value = std::shared_ptr<RDFValue>(new URN(AFF4_IMAGE_TYPE));
	std::unordered_set<URN> images = resolver.Query(URN(AFF4_TYPE), value);
	ASSERT_EQ(1, images.size());
	for(URN u : images){
		//std::cout << u.SerializeToString() << std::endl;
		ASSERT_EQ(std::string("aff4://f8c7d607-5a24-4759-9686-abb2394cf118"), u.SerializeToString());
	}
}

TEST(AFF4ImageRDFQuery, Sample3URN) {
	std::string filename = "samples/Base-Linear-ReadError.aff4";

	MemoryDataStore resolver;
	// This will open the container.
	URN urn = URN::NewURNFromFilename(filename);
	AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, urn);

	std::shared_ptr<RDFValue> value = std::shared_ptr<RDFValue>(new URN(AFF4_IMAGE_TYPE));
	std::unordered_set<URN> images = resolver.Query(URN(AFF4_TYPE), value);
	ASSERT_EQ(1, images.size());
	for(URN u : images){
		//std::cout << u.SerializeToString() << std::endl;
		ASSERT_EQ(std::string("aff4://d8dade4d-68e5-4cfd-a83a-69c88f9e95c0"), u.SerializeToString());
	}
}


