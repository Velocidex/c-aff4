from __future__ import division
from __future__ import print_function
from __future__ import absolute_import
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

from builtins import next
from builtins import str
from builtins import range
from past.utils import old_div
from builtins import object

import binascii
import collections
import hashlib
import six

from pyaff4 import container
from pyaff4 import data_store
from pyaff4 import hashes
from pyaff4 import lexicon
from pyaff4 import rdfvalue
from pyaff4 import zip


class InvalidBlockHashComparison(Exception):
    pass


class InvalidHashComparison(Exception):
    pass


class InconsistentHashMethod(Exception):
    pass


# the following is for ordering hashes when calculating

hashOrderingMap = { lexicon.HASH_MD5 : 1,
                    lexicon.HASH_SHA1: 2,
                    lexicon.HASH_SHA256 : 3,
                    lexicon.HASH_SHA512 : 4,
                    lexicon.HASH_BLAKE2B: 5}

class ValidationListener(object):
    def __init__(self):
        pass

    def onValidBlockHash(self, a):
        pass

    def onInvalidBlockHash(self, a, b, imageStreamURI, offset):
        raise InvalidBlockHashComparison(
            "Invalid block hash comarison for stream %s at offset %d" % (imageStreamURI, offset))

    def onValidHash(self, typ, hash, imageStreamURI):
        print("Validation of %s %s succeeded. Hash = %s" % (imageStreamURI, typ, hash))

    def onInvalidHash(self, typ, a, b, streamURI):
        raise InvalidHashComparison("Invalid %s comarison for stream %s" % (typ, streamURI))

class BlockHashesHash(object):
    def __init__(self, blockHashAlgo, hash, hashDataType):
        self.blockHashAlgo = blockHashAlgo
        self.hash = hash
        self.hashDataType = hashDataType

    def __eq__(self, other):
        if self.blockHashAlgo == other.blockHashAlgo and self.hash == other.hash and self.hashDataType == other.hashDataType:
            return True
        else:
            return False

    def __ne__(self, other):
        return not self.__eq__(other)

    def digest(self):
        return binascii.unhexlify(self.hash)


class Validator(object):
    def __init__(self, listener=None):
        if listener == None:
            self.listener = ValidationListener()
        else:
            self.listener = listener
        self.delegate = None

    def validateContainer(self, urn):
        lex = container.Container.identifyURN(urn)
        resolver = data_store.MemoryDataStore(lex)

        with zip.ZipFile.NewZipFile(resolver, urn) as zip_file:
            if lex == lexicon.standard:
                self.delegate = InterimStdValidator(resolver, lex, self.listener)
            elif lex == lexicon.legacy:
                self.delegate = PreStdValidator(resolver, lex, self.listener)
            else:
                raise ValueError

            self.delegate.doValidateContainer()

    def validateContainerMultiPart(self, urn_a, urn_b):
        # in this simple example, we assume that both files passed are
        # members of the Container
        lex = container.Container.identifyURN(urn_a)
        resolver = data_store.MemoryDataStore(lex)

        with zip.ZipFile.NewZipFile(resolver, urn_a) as zip_filea:
            with zip.ZipFile.NewZipFile(resolver, urn_b) as zip_fileb:
                if lex == lexicon.standard:
                    self.delegate = InterimStdValidator(resolver, lex, self.listener)
                elif lex == lexicon.legacy:
                    self.delegate = PreStdValidator(resolver, lex, self.listener)
                else:
                    raise ValueError

                self.delegate.doValidateContainer()

    def validateBlockMapHash(self, mapStreamURI, imageStreamURI):
        storedHash = next(self.resolver.QuerySubjectPredicate(
            mapStreamURI, self.lexicon.blockMapHash))
        calculalatedHash = self.calculateBlockMapHash(mapStreamURI, imageStreamURI, storedHash.datatype)

        if storedHash != calculalatedHash:
            self.listener.onInvalidHash("BlockMapHash", storedHash, calculalatedHash, mapStreamURI)
        else:
            self.listener.onValidHash("BlockMapHash", storedHash, mapStreamURI)

        return calculalatedHash

    def findLocalImageStreamOfMap(self, mapStreamURI):
        mapContainer = self.resolver.findContainerOfStream(mapStreamURI)
        for dependentStream in self.resolver.QuerySubjectPredicate(mapStreamURI,
                                                             self.lexicon.dependentStream):
            container = self.resolver.findContainerOfStream(dependentStream)
            if container == mapContainer:
                return dependentStream
        raise Exception

    def calculateBlockMapHash(self, mapStreamURI, imageStreamURI, storedHashDataType):
        storedBlockHashesHash = sorted(
            self.getStoredBlockHashes(str(imageStreamURI)),
            key=lambda x: hashOrderingMap[x.blockHashAlgo])

        calculatedHash = hashes.new(storedHashDataType)
        for hash in storedBlockHashesHash:
            bytes = hash.digest()
            calculatedHash.update(bytes)

        for hash in  self.resolver.QuerySubjectPredicate(mapStreamURI, self.lexicon.mapPointHash):
            calculatedHash.update(hash.digest())

        for hash in  self.resolver.QuerySubjectPredicate(mapStreamURI, self.lexicon.mapIdxHash):
            calculatedHash.update(hash.digest())

        for hash in self.resolver.QuerySubjectPredicate(mapStreamURI, self.lexicon.mapPathHash):
            calculatedHash.update(hash.digest())

        return hashes.newImmutableHash(calculatedHash.hexdigest(), storedHashDataType)

    def calculateBlockHashesHash(self, imageStreamURI):
        hash = self.getStoredBlockHashes(imageStreamURI)

        with self.resolver.AFF4FactoryOpen(imageStreamURI) as imageStream:

            calculatedBlockHashes = []
            for h in hash:
                calculatedBlockHashes.append(hashes.new(h.hashDataType))

            offset = 0
            while offset < imageStream.size:
                imageStream.seek(offset)
                block = imageStream.Read(imageStream.chunk_size)

                for i in range(len(hash)):
                    calculatedBlockHashesHash = calculatedBlockHashes[i]
                    hashDataType = hash[i].blockHashAlgo

                    # verify the block hash
                    h = hashes.new(hashDataType)
                    h.update(block)
                    calculatedBlockHash = h.hexdigest()

                    chunkIdx = old_div(offset, imageStream.chunk_size)
                    storedBlockHash = imageStream.readBlockHash(chunkIdx, hashDataType)
                    if calculatedBlockHash != storedBlockHash:
                        self.listener.onInvalidBlockHash(
                            calculatedBlockHash,
                            storedBlockHash.value,
                            imageStreamURI, offset)
                    else:
                        self.listener.onValidBlockHash(calculatedBlockHash)

                    calculatedBlockHashesHash.update(h.digest())

                offset = offset + imageStream.chunk_size

        # we now have the block hashes hash calculated
        res = []
        for i in range(len(hash)):
            a = hash[i].blockHashAlgo
            b = calculatedBlockHashes[i].hexdigest()
            c = hash[i].hashDataType
            blockHashesHash = BlockHashesHash(a, b, c)
            res.append(blockHashesHash)

        return res

    def getStoredBlockHashes(self, imageStreamURI):
        hashes = []
        for hash in self.resolver.QuerySubjectPredicate(imageStreamURI, self.lexicon.blockHashesHash):
            blockHashAlgo = hash.datatype
            digest = hash.value
            digestDataType = hash.datatype
            hashes.append(BlockHashesHash(blockHashAlgo, digest, digestDataType))

        return hashes

    def validateBlockHashesHash(self, imageStreamURI):
        storedHashes = self.getStoredBlockHashes(imageStreamURI)
        calculatedHashes = self.calculateBlockHashesHash(imageStreamURI)
        for i in range(len(storedHashes)):
            a = storedHashes[i]
            b = calculatedHashes[i]
            if a != b:
                self.listener.onInvalidHash("BlockHashesHash", a, b, imageStreamURI)
            else:
                self.listener.onValidHash("BlockHashesHash", a, imageStreamURI)

    def validateMapIdxHash(self, map_uri):
        for stored_hash in self.resolver.QuerySubjectPredicate(
                map_uri, self.lexicon.mapIdxHash):
            return self.validateSegmentHash(
                map_uri, "mapIdxHash", self.calculateMapIdxHash(
                    map_uri, stored_hash.datatype))

    def calculateMapIdxHash(self, mapURI, hashDataType):
        return self.calculateSegmentHash(mapURI, "idx", hashDataType)

    def validateMapPointHash(self, map_uri):
        for stored_hash in self.resolver.QuerySubjectPredicate(
                map_uri, self.lexicon.mapPointHash):
            return self.validateSegmentHash(
                map_uri, "mapPointHash", self.calculateMapPointHash(
                    map_uri, stored_hash.datatype))

    def calculateMapPointHash(self, mapURI, storedHashDataType):
        return self.calculateSegmentHash(mapURI, "map", storedHashDataType)

    def validateMapPathHash(self, map_uri):
        for stored_hash in self.resolver.QuerySubjectPredicate(
                map_uri, self.lexicon.mapPathHash):
            return self.validateSegmentHash(
                map_uri, "mapPathHash", self.calculateMapPathHash(
                    map_uri, stored_hash.datatype))

    def calculateMapPathHash(self, mapURI, storedHashDataType):
        return self.calculateSegmentHash(mapURI, "mapPath", storedHashDataType)

    def validateMapHash(self, map_uri):
        for stored_hash in self.resolver.QuerySubjectPredicate(
                map_uri, self.lexicon.mapHash):
            return self.validateSegmentHash(
                map_uri, "mapHash", self.calculateMapHash(
                    map_uri, stored_hash.datatype))

    def calculateMapHash(self, mapURI, storedHashDataType):
        calculatedHash = hashes.new(storedHashDataType)

        calculatedHash.update(self.readSegment(mapURI, "map"))
        calculatedHash.update(self.readSegment(mapURI, "idx"))

        try:
            calculatedHash.update(self.readSegment(mapURI, "mapPath"))
        except:
            pass

        return hashes.newImmutableHash(calculatedHash.hexdigest(), storedHashDataType)

    def validateSegmentHash(self, mapURI, hashType, calculatedHash):
        storedHash = next(self.resolver.QuerySubjectPredicate(mapURI, self.lexicon.base + hashType))

        if storedHash != calculatedHash:
            self.listener.onInvalidHash(hashType, storedHash, calculatedHash, mapURI)
        else:
            self.listener.onValidHash(hashType, storedHash, mapURI)


    def readSegment(self, parentURI, subSegment):
        parentURI = rdfvalue.URN(parentURI)
        segment_uri = parentURI.Append(subSegment)

        with self.resolver.AFF4FactoryOpen(segment_uri) as segment:
            data = segment.Read(segment.Size())
            return data

    def calculateSegmentHash(self, parentURI, subSegment, hashDataType):
        calculatedHash = hashes.new(hashDataType)

        data = self.readSegment(parentURI, subSegment)
        if data != None:
            calculatedHash.update(data)
            b = calculatedHash.hexdigest()
            return hashes.newImmutableHash(b, hashDataType)
        else:
            raise Exception

    def checkSame(self, a, b):
        if a != b:
            raise InconsistentHashMethod()


# A block hash validator for AFF4 Pre-Standard images produced by Evimetry 1.x-2.1
class PreStdValidator(Validator):
    def __init__(self, resolver, lex, listener=None):
        Validator.__init__(self, listener)
        self.resolver = resolver
        self.lexicon = lex

    def validateContainer(self, urn):
        with zip.ZipFile.NewZipFile(self.resolver, urn) as zip_file:
            self.doValidateContainer()

    # pre AFF4 standard Evimetry uses the contains relationship to locate the local
    # image stream of a Map
    def findLocalImageStreamOfMap(self, mapStreamURI):
        imageStreamURI = next(self.resolver.QuerySubjectPredicate(mapStreamURI,
                                                             self.lexicon.contains))
        return imageStreamURI

    def doValidateContainer(self):
        types = list(self.resolver.QueryPredicateObject(
            lexicon.AFF4_TYPE, self.lexicon.Image))

        if not types:
            return

        imageURI = types[0]

        # For block based hashing our starting point is the map

        if self.isMap(imageURI):
            with self.resolver.AFF4FactoryOpen(imageURI) as mapStream:
                for target in mapStream.targets:
                    if self.resolver.isImageStream(target):
                        self.validateBlockHashesHash(target)
                        self.validateMapIdxHash(imageURI)
                        self.validateMapPointHash(imageURI)
                        self.validateMapPathHash(imageURI)
                        self.validateMapHash(imageURI)
                        self.validateBlockMapHash(imageURI, target)

    # in AFF4 pre-standard Evimetry stores what we now call the blockMapHash in the Map, with the
    #  name blockHashesHash
    def validateBlockMapHash(self, mapStreamURI, imageStreamURI):

        storedHash = next(self.resolver.QuerySubjectPredicate(mapStreamURI,
                                                             self.lexicon.blockHashesHash))
        calculalatedHash = self.calculateBlockMapHash(mapStreamURI, imageStreamURI, storedHash.datatype)

        if storedHash != calculalatedHash:
            self.listener.onInvalidHash("BlockMapHash", storedHash, calculalatedHash, mapStreamURI)
        else:
            self.listener.onValidHash("BlockMapHash", storedHash, mapStreamURI)

    def isMap(self, stream):
        types = self.resolver.QuerySubjectPredicate(stream, lexicon.AFF4_TYPE)
        if self.lexicon.map in types:
            return True
        return False

# A block hash validator for AFF4 Interim Standard images produced by Evimetry 3.0
class InterimStdValidator(Validator):
    def __init__(self, resolver, lex, listener=None):
        Validator.__init__(self, listener)
        self.resolver = resolver
        self.lexicon = lex

    def validateContainer(self, urn):
        with zip.ZipFile.NewZipFile(self.resolver, urn) as zip_file:
            self.doValidateContainer()

    def getParentMap(self, imageStreamURI):
        imageStreamVolume = next(self.resolver.QuerySubjectPredicate(imageStreamURI, self.lexicon.stored))

        for map in self.resolver.QuerySubjectPredicate(imageStreamURI, self.lexicon.target):
            mapVolume = next(self.resolver.QuerySubjectPredicate(map, self.lexicon.stored))
            if mapVolume == imageStreamVolume:
                return map

        raise Exception("Illegal State")

    def doValidateContainer(self):
        # FIXME: This should further restrict by container URN since
        # the same data store may be used for multiple containers with
        # many images.
        for image in self.resolver.QueryPredicateObject(
                lexicon.AFF4_TYPE, self.lexicon.Image):

            datastreams = list(self.resolver.QuerySubjectPredicate(
                image, self.lexicon.dataStream))

            calculated_hashes = collections.OrderedDict()
            hash_datatype = None

            for stream in datastreams:
                if self.isMap(stream):
                    for image_stream_uri in self.resolver.QuerySubjectPredicate(
                            stream, self.lexicon.dependentStream):
                        parent_map = self.getParentMap(image_stream_uri)
                        if parent_map == stream:
                            # only validate the map and stream pair in the same container
                            self.validateBlockHashesHash(image_stream_uri)
                            self.validateMapIdxHash(parent_map)
                            self.validateMapPointHash(parent_map)
                            self.validateMapPathHash(parent_map)
                            self.validateMapHash(parent_map)

                            calculated_hash = self.validateBlockMapHash(
                                parent_map, image_stream_uri)
                            calculated_hashes[parent_map] = calculated_hash

                            # Assume all block hashes are the same type.
                            if (hash_datatype is not None and
                                hash_datatype != calculated_hash.datatype):
                                raise AttributeError(
                                    "Block hashes are not all the same type.")
                            else:
                                hash_datatype = calculated_hash.datatype

            for stored_hash in self.resolver.QuerySubjectPredicate(
                    image, self.lexicon.hash):
                hasha = ""
                hashb = ""
                parent_map = None

                # TODO: handle more cleanly the sematic difference between datatypes
                if len(calculated_hashes) == 1:
                    # This is a single part image
                    # The single AFF4 hash is just the blockMapHash
                    parent_map, calculated_hash = calculated_hashes.popitem()
                    hasha = stored_hash
                    hashb = calculated_hash

                else:
                    # This is a multiple part image The single AFF4
                    # hash is one layer up in the Merkel tree again,
                    # with the subordinate nodes being the
                    # blockMapHashes for the map stored in each
                    # container volume

                    # The hash algorithm we use for the single AFF4
                    # hash is the same algorithm we use for all of the
                    # Merkel tree inner nodes
                    current_hash = hashes.new(hash_datatype)

                    # FIXME: This is a flaw in the scheme since there
                    # is no reasonable order specified. We temporarily
                    # sort the results to get the test to pass but
                    # this needs to be properly addressed.

                    # We rely on the natural ordering of the map URN's
                    # as they are stored in the map to order the
                    # blockMapHashes in the Merkel tree.
                    for parent_map, calculated_hash in sorted(calculated_hashes.items()):
                        current_hash.update(calculated_hash.digest())

                    hasha = stored_hash.value
                    hashb = current_hash.hexdigest()

                if hasha != hashb:
                    self.listener.onInvalidHash("AFF4Hash", hasha, hashb, parent_map)
                else:
                    self.listener.onValidHash("AFF4Hash", hasha, parent_map)

    def getStoredBlockHashes(self, image_stream_uri):
        res = []

        for block_hash_uri in self.resolver.SelectSubjectsByPrefix(str(image_stream_uri) + "/blockhash."):
            for hash in self.resolver.QuerySubjectPredicate(block_hash_uri, self.lexicon.hash):
                extension = block_hash_uri.Parse().path.split(".")[-1]
                block_hash_algo_type = hashes.fromShortName(extension)
                hash = BlockHashesHash(block_hash_algo_type, hash.value, hash.datatype)
                res.append(hash)

        return res

    def isMap(self, stream):
        types = self.resolver.QuerySubjectPredicate(stream, lexicon.AFF4_TYPE)
        if self.lexicon.map in types:
            return True
        return False
