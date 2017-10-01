from __future__ import print_function
from __future__ import unicode_literals
# Copyright 2016,2017 Schatz Forensic Pty Ltd. All rights reserved.
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

from future import standard_library
standard_library.install_aliases()

import logging
import os
import io
import unittest

from pyaff4 import data_store
from pyaff4 import lexicon
from pyaff4 import plugins
from pyaff4 import rdfvalue
from pyaff4 import zip
from pyaff4 import hashes

LOGGER = logging.getLogger("pyaff4")

referenceImagesPath = os.path.join(os.path.dirname(__file__), "..", "test_images")
stdLinear = os.path.join(referenceImagesPath, "AFF4Std", "Base-Linear.aff4")

def conditional_on_images(f):
    if not os.access(stdLinear, os.R_OK):
        LOGGER.info("Test images not cloned into repository. Tests disabled."
                    "To enable type `git submodules init`")

        def _decorator():
            print (f.__name__ + ' has been disabled')

        return _decorator
    return f


class StandardsTest(unittest.TestCase):
    stdLinearURN = rdfvalue.URN.FromFileName(stdLinear)

    @conditional_on_images
    def testLocateImage(self):
        resolver = data_store.MemoryDataStore()

        with zip.ZipFile.NewZipFile(resolver, self.stdLinearURN) as zip_file:
            for subject in resolver.QueryPredicateObject(
                    "http://www.w3.org/1999/02/22-rdf-syntax-ns#type",
                    "http://aff4.org/Schema#DiskImage"):
                self.assertEquals(
                    subject,
                    "aff4://cf853d0b-5589-4c7c-8358-2ca1572b87eb")

            for subject in resolver.QueryPredicateObject(
                    "http://www.w3.org/1999/02/22-rdf-syntax-ns#type",
                    "http://aff4.org/Schema#Image"):
                self.assertEquals(
                    subject,
                    "aff4://cf853d0b-5589-4c7c-8358-2ca1572b87eb")

            for subject in resolver.QueryPredicateObject(
                    "http://www.w3.org/1999/02/22-rdf-syntax-ns#type",
                    "http://aff4.org/Schema#ContiguousImage"):
                self.assertEquals(
                    subject,
                    "aff4://cf853d0b-5589-4c7c-8358-2ca1572b87eb")

    @conditional_on_images
    def testReadMap(self):
        resolver = data_store.MemoryDataStore()

        with zip.ZipFile.NewZipFile(resolver, self.stdLinearURN) as zip_file:
            imageStream = resolver.AFF4FactoryOpen(
                "aff4://c215ba20-5648-4209-a793-1f918c723610")

            imageStream.Seek(0x163)
            res = imageStream.Read(17)
            self.assertEquals(res, b"Invalid partition")

    @conditional_on_images
    def testReadImageStream(self):
        resolver = data_store.MemoryDataStore()

        with zip.ZipFile.NewZipFile(resolver, self.stdLinearURN) as zip_file:
            mapStream = resolver.AFF4FactoryOpen(
                "aff4://c215ba20-5648-4209-a793-1f918c723610")

            mapStream.Seek(0x163)
            res = mapStream.Read(17)
            self.assertEquals(res, b"Invalid partition")


if __name__ == '__main__':
    unittest.main()
