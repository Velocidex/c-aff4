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
import StringIO
import unittest

from pyaff4 import aff4_image
from pyaff4 import data_store
from pyaff4 import lexicon
from pyaff4 import rdfvalue
from pyaff4 import zip

from pyaff4 import plugins


class AFF4ImageTest(unittest.TestCase):
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
                image_urn = self.volume_urn.Append(self.image_name)

                # Use default compression.
                with aff4_image.AFF4Image.NewAFF4Image(
                    resolver, image_urn, self.volume_urn) as image:
                    image.chunk_size = 10
                    image.chunks_per_segment = 3

                    for i in range(100):
                        image.Write("Hello world %02d!" % i)

                    self.image_urn = image.urn

                # Write a snappy compressed image.
                self.image_urn_2 = self.image_urn.Append("2")
                with aff4_image.AFF4Image.NewAFF4Image(
                    resolver, self.image_urn_2, self.volume_urn) as image_2:
                    image_2.compression = lexicon.AFF4_IMAGE_COMPRESSION_SNAPPY
                    image_2.Write("This is a test")

                # Use streaming API to write image.
                self.image_urn_3 = self.image_urn.Append("3")
                with aff4_image.AFF4Image.NewAFF4Image(
                    resolver, self.image_urn_3, self.volume_urn) as image:
                    image.chunk_size = 10
                    image.chunks_per_segment = 3
                    stream = StringIO.StringIO()
                    for i in range(100):
                        stream.write("Hello world %02d!" % i)

                    stream.seek(0)
                    image.WriteStream(stream)

    def testOpenImageByURN(self):
        resolver = data_store.MemoryDataStore()

        # This is required in order to load and parse metadata from this volume
        # into a fresh empty resolver.
        with zip.ZipFile.NewZipFile(resolver, self.filename) as zip_file:
            image_urn = zip_file.urn.Append(self.image_name)

        with resolver.AFF4FactoryOpen(image_urn) as image:
            self.assertEquals(image.chunk_size, 10)
            self.assertEquals(image.chunks_per_segment, 3)
            self.assertEquals(
                "Hello world 00!Hello world 01!Hello world 02!Hello world 03!"
                "Hello world 04!Hello world 05!Hello worl",
                image.Read(100))

            self.assertEquals(1500, image.Size())

        # Now test snappy decompression.
        with resolver.AFF4FactoryOpen(self.image_urn_2) as image_2:
            self.assertEquals(
                resolver.Get(image_2.urn, lexicon.AFF4_IMAGE_COMPRESSION),
                lexicon.AFF4_IMAGE_COMPRESSION_SNAPPY)

            data = image_2.Read(100)
            self.assertEquals(data, "This is a test")

        # Now test streaming API image.
        with resolver.AFF4FactoryOpen(self.image_urn_3) as image_3:
            self.assertEquals(image_3.chunk_size, 10)
            self.assertEquals(image_3.chunks_per_segment, 3)
            self.assertEquals(
                "Hello world 00!Hello world 01!Hello world 02!Hello world 03!"
                "Hello world 04!Hello world 05!Hello worl",
                image_3.Read(100))

if __name__ == '__main__':
    #logging.getLogger().setLevel(logging.DEBUG)
    unittest.main()
