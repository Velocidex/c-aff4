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
}


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
}


#if defined(HAVE_LIBYAML_CPP)
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
#endif

TEST_F(MemoryDataStoreTest, TurtleSerializationTest) {
  MemoryDataStore new_store;
  std::unique_ptr<AFF4Stream> output = StringIO::NewStringIO();
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

  std::vector<std::string> GetKeys() {
	  std::vector<std::string> result;
    for (AFF4ObjectCacheEntry *it=lru_list.next; it!=&lru_list; it=it->next) {
      result.push_back(it->key);
    };

    return result;
  };

  std::vector<std::string> GetInUse() {
	  std::vector<std::string> result;
    for(auto it: in_use) {
      result.push_back(it.first);
    };

    return result;
  };
};

TEST(AFF4ObjectCacheTest, TestLRU) {
  AFF4ObjectCacheMock cache(3);
  MemoryDataStore resolver;
  URN a = URN::NewURNFromFilename("a");
  URN b = URN::NewURNFromFilename("b");
  URN c = URN::NewURNFromFilename("c");
  URN d = URN::NewURNFromFilename("d");

  AFF4Object *obj1 = new AFF4Object(&resolver, a);
  AFF4Object *obj2 = new AFF4Object(&resolver, b);
  AFF4Object *obj3 = new AFF4Object(&resolver, c);
  AFF4Object *obj4 = new AFF4Object(&resolver, d);

  cache.Put(obj1);
  cache.Put(obj2);
  cache.Put(obj3);

  {
	  std::vector<std::string> result = cache.GetKeys();

    EXPECT_EQ(result[0], c.SerializeToString());
    EXPECT_EQ(result[1], b.SerializeToString());
    EXPECT_EQ(result[2], a.SerializeToString());
  };

  // This removes the object from the cache and places it in the in_use
  // list.
  EXPECT_EQ(cache.Get(a), obj1);
  {
	  std::vector<std::string> result = cache.GetKeys();
    EXPECT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], c.SerializeToString());
    EXPECT_EQ(result[1], b.SerializeToString());

    std::vector<std::string> in_use = cache.GetInUse();
    EXPECT_EQ(in_use.size(), 1);
    EXPECT_EQ(in_use[0], a.SerializeToString());

    // Now we return the object. It should now appear in the lru lists.
    cache.Return(obj1);
  };

  {
	  std::vector<std::string> result = cache.GetKeys();
    EXPECT_EQ(result.size(), 3);

    EXPECT_EQ(result[0], a.SerializeToString());
    EXPECT_EQ(result[1], c.SerializeToString());
    EXPECT_EQ(result[2], b.SerializeToString());

    std::vector<std::string> in_use = cache.GetInUse();
    EXPECT_EQ(in_use.size(), 0);
  }

  // Over flow the cache - this should expire the older object.
  cache.Put(obj4);

  {
	  std::vector<std::string> result = cache.GetKeys();
    EXPECT_EQ(result.size(), 3);

    EXPECT_EQ(result[0], d.SerializeToString());
    EXPECT_EQ(result[1], a.SerializeToString());
    EXPECT_EQ(result[2], c.SerializeToString());
  };

  // b is now expired so not in cache.
  EXPECT_EQ(cache.Get(b), (AFF4Object *)NULL);

  // Check that remove works
  cache.Remove(obj4);

  {
    EXPECT_EQ(cache.Get(d), (AFF4Object *)NULL);

    std::vector<std::string> result = cache.GetKeys();
    EXPECT_EQ(result.size(), 2);
  }
}
