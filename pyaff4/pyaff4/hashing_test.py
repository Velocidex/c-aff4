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
import os
import unittest

from pyaff4 import data_store
from pyaff4 import lexicon
from pyaff4 import plugins
from pyaff4 import rdfvalue
from pyaff4 import zip
from pyaff4 import hashes
from pyaff4 import block_hasher
from pyaff4 import linear_hasher


referenceImagesPath = os.path.join(os.path.dirname(__file__), u"..",
                                   u"test_images")
stdLinear = os.path.join(referenceImagesPath, u"AFF4Std", u"Base-Linear.aff4")
preStdLinear = os.path.join(referenceImagesPath, u"AFF4PreStd/Base-Linear.af4")
preStdAllocated = os.path.join(referenceImagesPath, u"AFF4PreStd",
                               u"Base-Allocated.af4")
stdLinear = os.path.join(referenceImagesPath, u"AFF4Std", u"Base-Linear.aff4")
stdAllocated = os.path.join(referenceImagesPath, u"AFF4Std",
                            u"Base-Allocated.aff4")
stdLinearAllHashes = os.path.join(referenceImagesPath,
                                  u"AFF4Std", u"Base-Linear-AllHashes.aff4")
stdLinearReadError = os.path.join(referenceImagesPath,
                                  u"AFF4Std", u"Base-Linear-ReadError.aff4")
stripedLinearA = os.path.join(referenceImagesPath,
                              u"AFF4Std", u"Striped", u"Base-Linear_1.aff4")
stripedLinearB = os.path.join(referenceImagesPath,
                              u"AFF4Std", u"Striped", u"Base-Linear_2.aff4")

def conditional_on_images(f):
    if not os.access(preStdLinear, os.R_OK):
        LOGGER.info("Test images not cloned into repository. Tests disabled."
                    "To enable type `git submodules init`")

        def _decorator():
            print (f.__name__ + ' has been disabled')

        return _decorator
    return f



class ValidatorTest(unittest.TestCase):
    preStdLinearURN = rdfvalue.URN.FromFileName(preStdLinear)
    preStdAllocatedURN = rdfvalue.URN.FromFileName(preStdAllocated)
    stdLinearURN = rdfvalue.URN.FromFileName(stdLinear)
    stdAllocatedURN = rdfvalue.URN.FromFileName(stdAllocated)
    stdLinearAllHashesURN = rdfvalue.URN.FromFileName(stdLinearAllHashes)
    stdLinearReadErrorURN = rdfvalue.URN.FromFileName(stdLinearReadError)
    stripedLinearAURN = rdfvalue.URN.FromFileName(stripedLinearA)
    stripedLinearBURN = rdfvalue.URN.FromFileName(stripedLinearB)

    @conditional_on_images
    def testBlockHashPreStdLinearImage(self):
        validator = block_hasher.Validator()
        validator.validateContainer(self.preStdLinearURN)

    @conditional_on_images
    def testLinearHashPreStdLinearImage(self):
        validator = linear_hasher.LinearHasher()
        hash = validator.hash(
            self.preStdLinearURN,
            u"aff4://085066db-6315-4369-a87e-bdc7bc777d45",
            lexicon.HASH_SHA1)
        print(dir(hash))
        print(hash.value)
        self.assertEqual(hash.value, "5d5f183ae7355b8dc8938b67aab77c0215c29ab4")

    @conditional_on_images
    def testLinearHashPreStdPartialAllocatedImage(self):
        validator = linear_hasher.LinearHasher()
        hash = validator.hash(
            self.preStdAllocatedURN,
            u"aff4://48a85e17-1041-4bcc-8b2b-7fb2cd4f815b", lexicon.HASH_SHA1)
        print(dir(hash))
        print(hash.value)
        self.assertEqual(hash.value, "a9f21b04a0a77613a5a34ecdd3af269464984035")

    @conditional_on_images
    def testBlockHashPreStdPartialAllocatedImage(self):
        validator = block_hasher.Validator()
        validator.validateContainer(self.preStdAllocatedURN)

    @conditional_on_images
    def testBlockHashStdLinearImage(self):
        validator = block_hasher.Validator()
        validator.validateContainer(self.stdLinearURN)

    @conditional_on_images
    def testBlockHashStdLinearReadError(self):
        validator = block_hasher.Validator()
        validator.validateContainer(self.stdLinearReadErrorURN)

    @conditional_on_images
    def testHashStdLinearImage(self):
        validator = linear_hasher.LinearHasher()
        hash = validator.hash(
            self.stdLinearURN,
            u"aff4://fcbfdce7-4488-4677-abf6-08bc931e195b", lexicon.HASH_SHA1)
        print(dir(hash))
        print(hash.value)
        self.assertEqual(hash.value, "7d3d27f667f95f7ec5b9d32121622c0f4b60b48d")

    @conditional_on_images
    def testHashStdLinearReadError(self):
        validator = linear_hasher.LinearHasher()
        hash = validator.hash(
            self.stdLinearReadErrorURN,
            u"aff4://b282d5f4-333a-4f6a-b96f-0e5138bb18c8", lexicon.HASH_SHA1)
        print(dir(hash))
        print(hash.value)
        self.assertEqual(hash.value, "67e245a640e2784ead30c1ff1a3f8d237b58310f")

    @conditional_on_images
    def testHashStdPartialAllocatedImage(self):
        validator = linear_hasher.LinearHasher()
        hash = validator.hash(
            self.stdAllocatedURN,
            u"aff4://e9cd53d3-b682-4f12-8045-86ba50a0239c", lexicon.HASH_SHA1)
        self.assertEqual(hash.value, "e8650e89b262cf0b4b73c025312488d5a6317a26")

    @conditional_on_images
    def testBlockHashStdLinearStriped(self):
        validator = block_hasher.Validator()
        validator.validateContainerMultiPart(self.stripedLinearBURN,
                                             self.stripedLinearAURN)

    @conditional_on_images
    def testHashStdLinearStriped(self):
        validator = linear_hasher.LinearHasher()
        hash = validator.hashMulti(
            self.stripedLinearBURN, self.stripedLinearAURN,
            u"aff4://2dd04819-73c8-40e3-a32b-fdddb0317eac", lexicon.HASH_SHA1)
        self.assertEqual(hash.value, "7d3d27f667f95f7ec5b9d32121622c0f4b60b48d")

    @conditional_on_images
    def testBlockHashStdContainerPartialAllocated(self):
        validator = block_hasher.Validator()
        validator.validateContainer(self.stdAllocatedURN)

    @conditional_on_images
    def testBlockHashPreStdLinearImage(self):
        validator = block_hasher.Validator()
        validator.validateContainer(self.preStdLinearURN)

    @conditional_on_images
    def testBlockHashStdLinearAllHashesImage(self):
        validator = block_hasher.Validator()
        validator.validateContainer(self.stdLinearAllHashesURN)

    @conditional_on_images
    def testHashStdLinearAllHashesImage(self):
        validator = linear_hasher.LinearHasher()
        hash = validator.hash(
            self.stdLinearAllHashesURN,
            u"aff4://2a497fe5-0221-4156-8b4d-176bebf7163f",
            lexicon.HASH_SHA1)

        print(dir(hash))
        print(hash.value)
        self.assertEqual(hash.value, "7d3d27f667f95f7ec5b9d32121622c0f4b60b48d")

if __name__ == '__main__':
    unittest.main()
