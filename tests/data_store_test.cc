#include <gtest/gtest.h>
#include "aff4/libaff4.h"
#include <iostream>

namespace aff4 {

class MemoryDataStoreTest: public ::testing::Test {
 protected:
  MemoryDataStore store;
};


TEST_F(MemoryDataStoreTest, IncompatibleGet) {
  RDFBytes result;

  store.Set(URN("hello"), URN("World"), new XSDString("foo"));

  // This should fail since the value is the wrong type.
  EXPECT_EQ(NOT_FOUND,
            store.Get(URN("hello"), URN("World"), result));
}


TEST_F(MemoryDataStoreTest, StorageTest) {
  XSDString result;

  store.Set(URN("hello"), URN("World"), new XSDString("foo"));

  EXPECT_EQ(STATUS_OK,
            store.Get(URN("hello"), URN("World"), result));

  EXPECT_STREQ(result.SerializeToString().c_str(), "foo");

  store.Set(URN("hello"), URN("World"), new XSDString("bar"));

  // In the current implementation a second Set() overwrites the previous value.
  EXPECT_EQ(STATUS_OK,
            store.Get(URN("hello"), URN("World"), result));

  EXPECT_STREQ(result.SerializeToString().c_str(), "bar");
}


TEST_F(MemoryDataStoreTest, TurtleSerializationTest) {
  store.Set(URN("hello"), URN("World"), new XSDString("foo"));

  MemoryDataStore new_store;
  std::unique_ptr<AFF4Stream> output = StringIO::NewStringIO();
  store.DumpToTurtle(*output, "");
  output->Seek(0, 0);

  // Load the new store with the serialized data.
  EXPECT_EQ(STATUS_OK,
            new_store.LoadFromTurtle(*output));

  XSDString result;
  EXPECT_EQ(STATUS_OK,
            store.Get(URN("hello"), URN("World"), result));

  EXPECT_STREQ(result.SerializeToString().c_str(), "foo");
}

} // namespace aff4
