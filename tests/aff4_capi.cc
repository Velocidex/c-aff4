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
#include <libaff4-c.h>
#include <unistd.h>
#include <glog/logging.h>

void printBuffer(char* buffer, int size) {
	for (int i = 0; i < size; i++) {
		if (i > 0) {
			printf(":");
		}
		char ch = buffer[i];
		printf("%02X", (ch & 0xff));
	}
	printf("\n");
}

TEST(AFF4CAPI, Sample1URN) {
	std::string filename = "samples/Base-Linear.af4";

	AFF4_init();

	int handle = AFF4_open(const_cast<char*>(filename.c_str()));
	ASSERT_NE(-1, handle);

	uint64_t size = AFF4_object_size(handle);
	ASSERT_EQ(268435456, size);

	void* buffer = malloc(32);
	memset(buffer, 0, 32);
	int read = AFF4_read(handle, 0, buffer, 32);
	ASSERT_EQ(32, read);

	printBuffer((char*) buffer, 32);
	free(buffer);
	AFF4_close(handle);

}

TEST(AFF4CAPI, Sample2URN) {
	std::string filename = "samples/Base-Allocated.af4";
	AFF4_init();

	int handle = AFF4_open(const_cast<char*>(filename.c_str()));
	ASSERT_NE(-1, handle);

	uint64_t size = AFF4_object_size(handle);
	ASSERT_EQ(268435456, size);

	void* buffer = malloc(32);
	memset(buffer, 0, 32);

	// Start
	int read = AFF4_read(handle, 0, buffer, 32);
	ASSERT_EQ(32, read);
	printBuffer((char*) buffer, 32);

	// Unreadable
	memset(buffer, 0, 32);
	read = AFF4_read(handle, 32326 * 512, buffer, 32);
	ASSERT_EQ(32, read);
	printBuffer((char*) buffer, 32);

	free(buffer);
	AFF4_close(handle);

}

TEST(AFF4CAPI, Sample3URN) {
	std::string filename = "samples/Base-Linear-ReadError.af4";
	AFF4_init();

	int handle = AFF4_open(const_cast<char*>(filename.c_str()));
	ASSERT_NE(-1, handle);

	uint64_t size = AFF4_object_size(handle);
	ASSERT_EQ(268435456, size);

	void* buffer = malloc(32);
	memset(buffer, 0, 32);

	// Start...
	int read = AFF4_read(handle, 0, buffer, 32);
	ASSERT_EQ(32, read);
	printBuffer((char*) buffer, 32);

	// Unreadable
	memset(buffer, 0, 32);
	read = AFF4_read(handle, 32326 * 512, buffer, 32);
	ASSERT_EQ(32, read);
	printBuffer((char*) buffer, 32);

	free(buffer);
	AFF4_close(handle);

}

