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


// Add a method to inspect protected internal state.
class AFF4ObjectCacheMock: public AFF4ObjectCache {
 public:
  AFF4ObjectCacheMock(size_t size): AFF4ObjectCache(size) {};

  vector<string> GetKeys() {
    vector<string> result;
    for(AFF4ObjectCacheEntry *it=lru_list.next; it!=&lru_list; it=it->next) {
      result.push_back(it->key);
    };

    return result;
  };

  vector<string> GetInUse() {
    vector<string> result;
    for(auto it: in_use) {
      result.push_back(it.first);
    };

    return result;
  };
};

TEST(AFF4ObjectCacheTest, TestLRU) {
  AFF4ObjectCacheMock cache(3);
  MemoryDataStore resolver;
  AFF4Object *obj1 = new AFF4Object(&resolver, "a");
  AFF4Object *obj2 = new AFF4Object(&resolver, "b");
  AFF4Object *obj3 = new AFF4Object(&resolver, "c");
  AFF4Object *obj4 = new AFF4Object(&resolver, "d");

  cache.Put(obj1);
  cache.Put(obj2);
  cache.Put(obj3);

  {
    vector<string> result = cache.GetKeys();

    EXPECT_EQ(result[0], "file:///c");
    EXPECT_EQ(result[1], "file:///b");
    EXPECT_EQ(result[2], "file:///a");
  };

  // This removes the object from the cache and places it in the in_use
  // list.
  EXPECT_EQ(cache.Get("file:///a"), obj1);
  {
    vector<string> result = cache.GetKeys();
    EXPECT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], "file:///c");
    EXPECT_EQ(result[1], "file:///b");

    vector<string> in_use = cache.GetInUse();
    EXPECT_EQ(in_use.size(), 1);
    EXPECT_EQ(in_use[0], "file:///a");

    // Now we return the object. It should now appear in the lru lists.
    cache.Return(obj1);
  };

  {
    vector<string> result = cache.GetKeys();
    EXPECT_EQ(result.size(), 3);

    EXPECT_EQ(result[0], "file:///a");
    EXPECT_EQ(result[1], "file:///c");
    EXPECT_EQ(result[2], "file:///b");

    vector<string> in_use = cache.GetInUse();
    EXPECT_EQ(in_use.size(), 0);
  }

  // Over flow the cache - this should expire the older object.
  cache.Put(obj4);

  {
    vector<string> result = cache.GetKeys();
    EXPECT_EQ(result.size(), 3);

    EXPECT_EQ(result[0], "file:///d");
    EXPECT_EQ(result[1], "file:///a");
    EXPECT_EQ(result[2], "file:///c");
  };

  // b is now expired so not in cache.
  EXPECT_EQ(cache.Get("file:///b"), (AFF4Object *)NULL);

  // Check that remove works
  cache.Remove(obj4);

  {
    EXPECT_EQ(cache.Get("file:///d"), (AFF4Object *)NULL);

    vector<string> result = cache.GetKeys();
    EXPECT_EQ(result.size(), 2);
  }
};
