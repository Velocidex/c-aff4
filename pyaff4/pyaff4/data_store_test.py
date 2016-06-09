# Copyright 2015 Google Inc. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations under
# the License.


from pyaff4 import aff4
from pyaff4 import data_store
from pyaff4 import lexicon
from pyaff4 import rdfvalue
import unittest

import StringIO


class DataStoreTest(unittest.TestCase):
    def setUp(self):
        self.store = data_store.MemoryDataStore()
        self.store.Set(
            rdfvalue.URN("hello"), rdfvalue.URN("World"),
            rdfvalue.XSDString("foo"))

        self.store.Set(
            rdfvalue.URN("hello"), rdfvalue.URN(lexicon.AFF4_TYPE),
            rdfvalue.XSDString("bar"))

    def testDataStore(self):
        result = self.store.Get(rdfvalue.URN("hello"), rdfvalue.URN("World"))
        self.assertEquals(type(result), rdfvalue.XSDString)

        self.assertEquals(result.SerializeToString(), "foo")

        self.store.Set(
            rdfvalue.URN("hello"), rdfvalue.URN("World"),
            rdfvalue.XSDString("bar"))

        # In the current implementation a second Set() overwrites the previous
        # value.
        self.assertEquals(
            self.store.Get(rdfvalue.URN("hello"), rdfvalue.URN("World")),
            rdfvalue.XSDString("bar"))

    def testTurtleSerialization(self):
        data = self.store.DumpToTurtle(verbose=True)
        new_store = data_store.MemoryDataStore()
        new_store.LoadFromTurtle(StringIO.StringIO(data))
        res = new_store.Get(rdfvalue.URN("hello"), rdfvalue.URN("World"))
        self.assertEquals(res, "foo")

class AFF4ObjectCacheMock(data_store.AFF4ObjectCache):
    def GetKeys(self):
        return [entry.key for entry in self.lru_list]

    def GetInUse(self):
        return [key for key in self.in_use]


class AFF4ObjectCacheTest(unittest.TestCase):
    def testLRU(self):
        cache = AFF4ObjectCacheMock(3)
        resolver = data_store.MemoryDataStore()

        obj1 = aff4.AFF4Object(resolver, "a")
        obj2 = aff4.AFF4Object(resolver, "b")
        obj3 = aff4.AFF4Object(resolver, "c")
        obj4 = aff4.AFF4Object(resolver, "d")

        cache.Put(obj1)
        cache.Put(obj2)
        cache.Put(obj3)

        result = cache.GetKeys()

        self.assertEquals(result[0], "file:///c")
        self.assertEquals(result[1], "file:///b")
        self.assertEquals(result[2], "file:///a")

        # This removes the object from the cache and places it in the in_use
        # list.
        self.assertEquals(cache.Get("file:///a"), obj1)

        result = cache.GetKeys()
        self.assertEquals(len(result), 2)
        self.assertEquals(result[0], "file:///c")
        self.assertEquals(result[1], "file:///b")

        in_use = cache.GetInUse()
        self.assertEquals(len(in_use), 1)
        self.assertEquals(in_use[0], "file:///a")

        # Now we return the object. It should now appear in the lru lists.
        cache.Return(obj1)

        result = cache.GetKeys()
        self.assertEquals(len(result), 3)

        self.assertEquals(result[0], "file:///a")
        self.assertEquals(result[1], "file:///c")
        self.assertEquals(result[2], "file:///b")

        in_use = cache.GetInUse()
        self.assertEquals(len(in_use), 0)

        # Over flow the cache - this should expire the older object.
        cache.Put(obj4)
        result = cache.GetKeys()
        self.assertEquals(len(result), 3)

        self.assertEquals(result[0], "file:///d")
        self.assertEquals(result[1], "file:///a")
        self.assertEquals(result[2], "file:///c")

        # b is now expired so not in cache.
        self.assertEquals(cache.Get("file:///b"), None)

        # Check that remove works
        cache.Remove(obj4)

        self.assertEquals(cache.Get("file:///d"), None)
        result = cache.GetKeys()
        self.assertEquals(len(result), 2)


if __name__ == '__main__':
    unittest.main()
