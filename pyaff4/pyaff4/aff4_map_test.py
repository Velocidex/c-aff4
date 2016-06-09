# Copyright 2014 Google Inc. All rights reserved.
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

import os
import unittest

from pyaff4 import aff4_file
from pyaff4 import aff4_map
from pyaff4 import data_store
from pyaff4 import lexicon
from pyaff4 import rdfvalue
from pyaff4 import zip


class AFF4MapTest(unittest.TestCase):
    filename = "/tmp/aff4_test.zip"
    image_name = "image.dd"

    def tearDown(self):
        try:
            os.unlink(self.filename)
        except (IOError, OSError):
            pass

    def setUp(self):
        with data_store.MemoryDataStore() as resolver:
            resolver.Set(self.filename, lexicon.AFF4_STREAM_WRITE_MODE,
                         rdfvalue.XSDString("truncate"))

            with zip.ZipFile.NewZipFile(resolver, self.filename) as zip_file:
                self.volume_urn = zip_file.urn
                self.image_urn = self.volume_urn.Append(self.image_name)

                #  # Write Map image sequentially (Seek/Write method).
                with aff4_map.AFF4Map.NewAFF4Map(
                    resolver, self.image_urn, self.volume_urn) as image:
                    # Maps are written in random order.
                    image.Seek(50)
                    image.Write("XX - This is the position.")

                    image.Seek(0)
                    image.Write("00 - This is the position.")

                    # We can "overwrite" data by writing the same range again.
                    image.Seek(50)
                    image.Write("50")

                # Test the Stream method.
                with resolver.CachePut(
                        aff4_file.AFF4MemoryStream(resolver)) as source:
                    # Fill it with data.
                    source.Write("AAAABBBBCCCCDDDDEEEEFFFFGGGGHHHH")

                    # Make a temporary map that defines our plan.
                    helper_map = aff4_map.AFF4Map(resolver)

                    helper_map.AddRange(4, 0, 4, source.urn)  # 0000AAAA
                    helper_map.AddRange(0, 12, 4, source.urn) # DDDDAAAA
                    helper_map.AddRange(12, 16, 4, source.urn)# DDDDAAAA0000EEEE

                    image_urn_2 = self.volume_urn.Append(
                        self.image_name).Append("streamed")

                    with aff4_map.AFF4Map.NewAFF4Map(
                        resolver, image_urn_2, self.volume_urn) as image:

                        # Now we create the real map by copying the temporary
                        # map stream.
                        image.WriteStream(helper_map)

    def testAddRange(self):
        resolver = data_store.MemoryDataStore()

        # This is required in order to load and parse metadata from this volume
        # into a fresh empty resolver.
        with zip.ZipFile.NewZipFile(resolver, self.filename) as zip_file:
            image_urn = zip_file.urn.Append(self.image_name)

        with resolver.AFF4FactoryOpen(image_urn) as map:
            # First test - overlapping regions:
            map.AddRange(0, 0, 100, "a")
            map.AddRange(10, 10, 100, "a")

            # Should be merged into a single range.
            ranges = map.GetRanges()
            self.assertEquals(len(ranges), 1)
            self.assertEquals(ranges[0].length, 110)

            map.Clear()

            # Repeating regions - should not be merged but first region should
            # be truncated.
            map.AddRange(0, 0, 100, "a")
            map.AddRange(50, 0, 100, "a")

            ranges = map.GetRanges()
            self.assertEquals(len(ranges), 2)
            self.assertEquals(ranges[0].length, 50)

            # Inserted region. Should split existing region into three.
            map.Clear()

            map.AddRange(0, 0, 100, "a")
            map.AddRange(50, 0, 10, "b")

            ranges = map.GetRanges()
            self.assertEquals(len(ranges), 3)
            self.assertEquals(ranges[0].length, 50)
            self.assertEquals(ranges[0].target_id, 0)

            self.assertEquals(ranges[1].length, 10)
            self.assertEquals(ranges[1].target_id, 1)

            self.assertEquals(ranges[2].length, 40)
            self.assertEquals(ranges[2].target_id, 0)

            # New range overwrites all the old ranges.
            map.AddRange(0, 0, 100, "b")

            ranges = map.GetRanges()
            self.assertEquals(len(ranges), 1)
            self.assertEquals(ranges[0].length, 100)
            self.assertEquals(ranges[0].target_id, 1)


            # Simulate writing contiguous regions. These should be merged into a
            # single region automatically.
            map.Clear()

            map.AddRange(0, 100, 10, "a")
            map.AddRange(10, 110, 10, "a")
            map.AddRange(20, 120, 10, "a")
            map.AddRange(30, 130, 10, "a")

            ranges = map.GetRanges()
            self.assertEquals(len(ranges), 1)
            self.assertEquals(ranges[0].length, 40)
            self.assertEquals(ranges[0].target_id, 0)

            # Writing sparse image.
            map.Clear()

            map.AddRange(0, 100, 10, "a")
            map.AddRange(30, 130, 10, "a")

            ranges = map.GetRanges()
            self.assertEquals(len(ranges), 2)
            self.assertEquals(ranges[0].length, 10)
            self.assertEquals(ranges[0].target_id, 0)
            self.assertEquals(ranges[1].length, 10)
            self.assertEquals(ranges[1].map_offset, 30)
            self.assertEquals(ranges[1].target_id, 0)

            # Now merge. Adding the missing region makes the image not sparse.
            map.AddRange(10, 110, 20, "a")
            ranges = map.GetRanges()
            self.assertEquals(len(ranges), 1)
            self.assertEquals(ranges[0].length, 40)

    def testCreateMapStream(self):
        resolver = data_store.MemoryDataStore()

        # This is required in order to load and parse metadata from this volume
        # into a fresh empty resolver.
        with zip.ZipFile.NewZipFile(resolver, self.filename) as zip_file:
            image_urn = zip_file.urn.Append(self.image_name)
            image_urn_2 = image_urn.Append("streamed")

        # Check the first stream.
        self.CheckImageURN(resolver, image_urn)

        # The second stream must be the same.
        self.CheckStremImageURN(resolver, image_urn_2)

    def CheckStremImageURN(self, resolver, image_urn_2):
        with resolver.AFF4FactoryOpen(image_urn_2) as map:
            self.assertEquals(map.Size(), 16)
            self.assertEquals(map.Read(100), "DDDDAAAA\x00\x00\x00\x00EEEE")

        # The data stream should be packed without gaps.
        with resolver.AFF4FactoryOpen(image_urn_2.Append("data")) as image:
            self.assertEquals(image.Read(100), "DDDDAAAAEEEE")

    def CheckImageURN(self, resolver, image_urn):
        with resolver.AFF4FactoryOpen(image_urn) as map:
            map.Seek(50)
            self.assertEquals(map.Read(2), "50")

            map.Seek(0)
            self.assertEquals(map.Read(2), "00")

            ranges = map.GetRanges()
            self.assertEquals(len(ranges), 3)
            self.assertEquals(ranges[0].length, 26)
            self.assertEquals(ranges[0].map_offset, 0)
            self.assertEquals(ranges[0].target_offset, 26)

            # This is the extra "overwritten" 2 bytes which were appended to the
            # end of the target stream and occupy the map range from 50-52.
            self.assertEquals(ranges[1].length, 2)
            self.assertEquals(ranges[1].map_offset, 50)
            self.assertEquals(ranges[1].target_offset, 52)

            self.assertEquals(ranges[2].length, 24)
            self.assertEquals(ranges[2].map_offset, 52)
            self.assertEquals(ranges[2].target_offset, 2)

            # Test that reads outside the ranges null pad correctly.
            map.Seek(48)
            read_string = map.Read(4)
            self.assertEquals(read_string, "\x00\x0050")


if __name__ == '__main__':
    #logging.getLogger().setLevel(logging.DEBUG)
    unittest.main()
