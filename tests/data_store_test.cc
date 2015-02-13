#include <gtest/gtest.h>
#include <libaff4.h>
#include <iostream>

class MemoryDataStoreTest: public ::testing::Test {
 protected:
  MemoryDataStore store;

  virtual void SetUp() {
    store.Set(URN("hello"), URN("World"), new XSDString("foo"));
  };
};


TEST_F(MemoryDataStoreTest, IncompatibleGet) {
  RDFBytes result;

  // This should fail since the value is the wrong type.
  EXPECT_EQ(INCOMPATIBLE_TYPES,
            store.Get(URN("hello"), URN("World"), result));
};


TEST_F(MemoryDataStoreTest, StorageTest) {
  XSDString result;

  EXPECT_EQ(STATUS_OK,
            store.Get(URN("hello"), URN("World"), result));

  EXPECT_STREQ(result.SerializeToString().c_str(), "foo");

  store.Set(URN("hello"), URN("World"), new XSDString("bar"));

  // In the current implementation a second Set() overwrites the previous value.
  EXPECT_EQ(STATUS_OK,
            store.Get(URN("hello"), URN("World"), result));

  EXPECT_STREQ(result.SerializeToString().c_str(), "bar");
};


TEST_F(MemoryDataStoreTest, YamlSerializationTest) {
  MemoryDataStore new_store;
  unique_ptr<AFF4Stream> output = StringIO::NewStringIO();

  store.DumpToYaml(*output);
  output->Seek(0, 0);

  // Load the new store with the serialized data. For now YAML support is not
  // fully implemented.
  EXPECT_EQ(NOT_IMPLEMENTED,
            new_store.LoadFromYaml(*output));
}


TEST_F(MemoryDataStoreTest, TurtleSerializationTest) {
  MemoryDataStore new_store;
  unique_ptr<AFF4Stream> output = StringIO::NewStringIO();
  XSDString result;

  store.DumpToTurtle(*output, "");
  output->Seek(0, 0);

  // Load the new store with the serialized data.
  EXPECT_EQ(STATUS_OK,
            new_store.LoadFromTurtle(*output));

  EXPECT_EQ(STATUS_OK,
            store.Get(URN("hello"), URN("World"), result));

  EXPECT_STREQ(result.SerializeToString().c_str(), "foo");
}
