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

import unittest

from pyaff4 import aff4_directory
from pyaff4 import aff4_utils
from pyaff4 import data_store
from pyaff4 import lexicon
from pyaff4 import rdfvalue

from pyaff4 import plugins


class AFF4DirectoryTest(unittest.TestCase):
    root_path = "/tmp/aff4_directory/"
    segment_name = "Foobar.txt"

    def tearDown(self):
        aff4_utils.RemoveDirectory(self.root_path)

    def setUp(self):
        with data_store.MemoryDataStore() as resolver:
            root_urn = rdfvalue.URN.NewURNFromFilename(self.root_path)

            resolver.Set(root_urn, lexicon.AFF4_STREAM_WRITE_MODE,
                         rdfvalue.XSDString("truncate"))

            with aff4_directory.AFF4Directory.NewAFF4Directory(
                    resolver, root_urn) as volume:

                segment_urn = volume.urn.Append(self.segment_name)
                with volume.CreateMember(segment_urn) as member:
                    member.Write("Hello world")
                    resolver.Set(
                        member.urn, lexicon.AFF4_STREAM_ORIGINAL_FILENAME,
                        rdfvalue.XSDString(self.root_path + self.segment_name))

    def testCreateMember(self):
        with data_store.MemoryDataStore() as resolver:
            root_urn = rdfvalue.URN.NewURNFromFilename(self.root_path)
            with aff4_directory.AFF4Directory.NewAFF4Directory(
                    resolver, root_urn) as directory:

                # Check for member.
                child_urn = directory.urn.Append(self.segment_name)
                with resolver.AFF4FactoryOpen(child_urn) as child:
                    self.assertEquals(child.Read(10000), "Hello world")

                # Check that the metadata is carried over.
                filename = resolver.Get(
                    child_urn, lexicon.AFF4_STREAM_ORIGINAL_FILENAME)

                self.assertEquals(filename, self.root_path + self.segment_name)

if __name__ == '__main__':
    #logging.getLogger().setLevel(logging.DEBUG)
    unittest.main()
