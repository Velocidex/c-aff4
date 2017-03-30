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

import unittest

from pyaff4 import data_store
from pyaff4 import lexicon
from pyaff4 import plugins
from pyaff4 import rdfvalue
from pyaff4 import zip
from pyaff4 import hashes
from pyaff4.block_hasher import *
from pyaff4.linear_hasher import *

class ValidatorTest(unittest.TestCase):
    referenceImagesPath = "/Users/bradley/git/ReferenceImages/"

    preStdLinear = referenceImagesPath + "/AFF4PreStd/Base-Linear.af4"
    preStdAllocated = referenceImagesPath + "/AFF4PreStd/Base-Allocated.af4"
    stdLinear = referenceImagesPath + "/AFF4Std/Base-Linear.aff4"
    stdAllocated = referenceImagesPath + "/AFF4Std/Base-Allocated.aff4"
    stdLinearAllHashes = referenceImagesPath + "/AFF4Std/Base-Linear-AllHashes.aff4"
    stdLinearReadError = referenceImagesPath + "/AFF4Std/Base-Linear-ReadError.aff4"
    stripedLinearA = referenceImagesPath + "/AFF4Std/Striped/Base-Linear_1.aff4"
    stripedLinearB = referenceImagesPath + "/AFF4Std/Striped/Base-Linear_2.aff4"

    def testBlockHashPreStdLinearImage(self):
        validator = Validator()
        validator.validateContainer(self.preStdLinear)

    def testLinearHashPreStdLinearImage(self):
        validator = LinearHasher()
        hash = validator.hash(self.preStdLinear, "aff4://085066db-6315-4369-a87e-bdc7bc777d45", lexicon.HASH_SHA1)
        print dir(hash)
        print hash.value
        self.assertEqual(hash.value, "5d5f183ae7355b8dc8938b67aab77c0215c29ab4")

    def testLinearHashPreStdPartialAllocatedImage(self):
        validator = LinearHasher()
        hash = validator.hash(self.preStdAllocated, "aff4://48a85e17-1041-4bcc-8b2b-7fb2cd4f815b", lexicon.HASH_SHA1)
        print dir(hash)
        print hash.value
        self.assertEqual(hash.value, "a9f21b04a0a77613a5a34ecdd3af269464984035")

    def testBlockHashPreStdPartialAllocatedImage(self):
        validator = Validator()
        validator.validateContainer(self.preStdAllocated)

    def testBlockHashStdLinearImage(self):
        validator = Validator()
        validator.validateContainer(self.stdLinear)

    def testBlockHashStdLinearReadError(self):
        validator = Validator()
        validator.validateContainer(self.stdLinearReadError)

    def testHashStdLinearImage(self):
        validator = LinearHasher()
        hash = validator.hash(self.stdLinear, "aff4://fcbfdce7-4488-4677-abf6-08bc931e195b", lexicon.HASH_SHA1)
        print dir(hash)
        print hash.value
        self.assertEqual(hash.value, "7d3d27f667f95f7ec5b9d32121622c0f4b60b48d")

    def testHashStdLinearReadError(self):
        validator = LinearHasher()
        hash = validator.hash(self.stdLinearReadError, "aff4://b282d5f4-333a-4f6a-b96f-0e5138bb18c8", lexicon.HASH_SHA1)
        print dir(hash)
        print hash.value
        self.assertEqual(hash.value, "67e245a640e2784ead30c1ff1a3f8d237b58310f")

    def testHashStdPartialAllocatedImage(self):
        validator = LinearHasher()
        hash = validator.hash(self.stdAllocated, "aff4://e9cd53d3-b682-4f12-8045-86ba50a0239c", lexicon.HASH_SHA1)
        self.assertEqual(hash.value, "e8650e89b262cf0b4b73c025312488d5a6317a26")

    def testBlockHashStdLinearStriped(self):
        validator = Validator()
        validator.validateContainerMultiPart(self.stripedLinearB, self.stripedLinearA)

    def testHashStdLinearStriped(self):
        validator = LinearHasher()
        hash = validator.hashMulti(self.stripedLinearB, self.stripedLinearA, "aff4://2dd04819-73c8-40e3-a32b-fdddb0317eac", lexicon.HASH_SHA1)
        self.assertEqual(hash.value, "7d3d27f667f95f7ec5b9d32121622c0f4b60b48d")

    def testBlockHashStdContainerPartialAllocated(self):
        validator = Validator()
        validator.validateContainer(self.stdAllocated)

    def testBlockHashPreStdLinearImage(self):
        validator = Validator()
        validator.validateContainer(self.preStdLinear)

    def testBlockHashStdLinearAllHashesImage(self):
        validator = Validator()
        validator.validateContainer(self.stdLinearAllHashes)

    def testHashStdLinearAllHashesImage(self):
        validator = LinearHasher()
        hash = validator.hash(self.stdLinearAllHashes, "aff4://2a497fe5-0221-4156-8b4d-176bebf7163f", lexicon.HASH_SHA1)
        print dir(hash)
        print hash.value
        self.assertEqual(hash.value, "7d3d27f667f95f7ec5b9d32121622c0f4b60b48d")

if __name__ == '__main__':
    unittest.main()