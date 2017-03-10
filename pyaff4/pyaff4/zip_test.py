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


import os
import StringIO
import unittest

from pyaff4 import data_store
from pyaff4 import lexicon
from pyaff4 import plugins
from pyaff4 import rdfvalue
from pyaff4 import zip



class ZipTest(unittest.TestCase):
    filename = "/tmp/aff4_test.zip"
    segment_name = "Foobar.txt"
    streamed_segment = "streamed.txt"
    data1 = "I am a segment!"
    data2 = "I am another segment!"

    def setUp(self):
        with data_store.MemoryDataStore() as resolver:
            resolver.Set(self.filename, lexicon.AFF4_STREAM_WRITE_MODE,
                         rdfvalue.XSDString("truncate"))

            with zip.ZipFile.NewZipFile(resolver, self.filename) as zip_file:
                self.volume_urn = zip_file.urn
                segment_urn = self.volume_urn.Append(self.segment_name)

                with zip_file.CreateMember(segment_urn) as segment:
                    segment.Write(self.data1)

                with zip_file.CreateMember(segment_urn) as segment2:
                    segment2.Seek(0, 2)
                    segment2.Write(self.data2)

                streamed_urn = self.volume_urn.Append(self.streamed_segment)
                with zip_file.CreateMember(streamed_urn) as streamed:
                    streamed.compression_method = zip.ZIP_DEFLATE
                    src = StringIO.StringIO(self.data1)
                    streamed.WriteStream(src)

    def tearDown(self):
        try:
            os.unlink(self.filename)
        except (IOError, OSError):
            pass

    def testStreamedSegment(self):
        resolver = data_store.MemoryDataStore()

        # This is required in order to load and parse metadata from this volume
        # into a fresh empty resolver.
        with zip.ZipFile.NewZipFile(resolver, self.filename) as zip_file:
            segment_urn = zip_file.urn.Append(self.streamed_segment)

        with resolver.AFF4FactoryOpen(segment_urn) as segment:
            self.assertEquals(segment.Read(1000), self.data1)

    def testOpenSegmentByURN(self):
        resolver = data_store.MemoryDataStore()

        # This is required in order to load and parse metadata from this volume
        # into a fresh empty resolver.
        with zip.ZipFile.NewZipFile(resolver, self.filename) as zip_file:
            segment_urn = zip_file.urn.Append(self.segment_name)

        with resolver.AFF4FactoryOpen(segment_urn) as segment:
            self.assertEquals(segment.Read(1000), self.data1 + self.data2)

if __name__ == '__main__':
    unittest.main()
