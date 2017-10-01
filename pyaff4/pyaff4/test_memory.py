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

from builtins import str
import unittest

from pyaff4 import data_store
from pyaff4 import lexicon
from pyaff4 import plugins
from pyaff4 import rdfvalue
from pyaff4 import zip
from pyaff4 import hashes
from pyaff4.block_hasher import *
from pyaff4.linear_hasher import *
from volatility.conf import ConfObject
from volatility.plugins.addrspaces import aff4

class ValidatorTest(unittest.TestCase):
    referenceImagesPath = "/Users/bradley/Desktop/LowrieSR/"
    referenceImagesPath2 = "/Users/bradley/Desktop/Images/"

    memoryImage= referenceImagesPath + "SRLowrie.MacBook.New.pmem.af4"

    memoryImage2 = referenceImagesPath2 + "MaverickPMem_PhysicalMemory.aff4"

    memoryImage3 = referenceImagesPath2 + "win10.aff4"


    def testLinearHashPreStdLinearImage(self):
        lex = Container.identify(self.memoryImage)
        resolver = data_store.MemoryDataStore(lex)

        validator = LinearHasher()
        hash = validator.hash(self.memoryImage, "aff4://ad0c6ce0-1e1c-4e8f-8114-7f876237138f/dev/pmem", lexicon.HASH_SHA1)
        print(dir(hash))
        print(hash.value)
        self.assertEqual(hash.value, "5d5f183ae7355b8dc8938b67aab77c0215c29ab4")

    def testLinearHashPreStdLinearImage2(self):
        lex = Container.identify(self.memoryImage2)
        resolver = data_store.MemoryDataStore(lex)

        validator = LinearHasher()
        hash = validator.hash(self.memoryImage2, "aff4://a862d4b0-ff3d-4ccf-a1e9-5316a4f7b8fe", lexicon.HASH_SHA1)
        print(dir(hash))
        print(hash.value)
        self.assertEqual(hash.value, "8667a82bafa7b4b3513838aac31bbd20498afe3f")


    def testOpenMemoryImage3(self):

        cont = Container.open(self.memoryImage3)

        for run in cont.GetRanges():
            print(str(run))

        address = 0x2b708ac
        baseBlock = 0x2900000

        offset = address - baseBlock

        tenM = 10 * 1024 * 1024


        cont.seek(baseBlock)
        data = cont.read(tenM)
        self.assertTrue(len(data) == tenM)
        idle = data[offset:offset+4]

        self.assertEqual(idle, "Idle")


    def testOpenPMEMMacImage(self):
        cont = Container.open(self.memoryImage)

    def testOpenRekallWin10Image(self):
        cont = Container.open(self.memoryImage3)


