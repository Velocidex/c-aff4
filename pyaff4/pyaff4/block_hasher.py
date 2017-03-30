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

from pyaff4 import data_store
from pyaff4 import hashes
from pyaff4 import lexicon
from pyaff4 import rdfvalue
from pyaff4.container import Container

import zip
import hashlib

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

def hashLengthComparator(a, b):
    lengtha = hashOrderingMap[a.blockHashAlgo]
    lengthb = hashOrderingMap[b.blockHashAlgo]

    if lengtha < lengthb:
        return -1
    elif lengtha > lengthb:
        return 1
    else:
        return 0

class ValidationListener:
    def __init__(self):
        pass

    def onValidBlockHash(self, a):
        pass

    def onInValidBlockHash(self, a, b, imageStreamURI, offset):
        raise InvalidBlockHashComparison(
            "Invalid block hash comarison for stream %s at offset %d" % (imageStreamURI, offset))

    def onValidHash(self, typ, hash, imageStreamURI):
        print "Validation of %s %s succeeded. Hash = %s" % (imageStreamURI, typ, hash)

    def onInvalidHash(self, typ, a, b, streamURI):
        raise InvalidHashComparison("Invalid %s comarison for stream %s" % (typ, streamURI))

class BlockHashesHash:
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
        return self.hash.decode('hex')


class Validator:
    def __init__(self, listener=None):
        if listener == None:
            self.listener = ValidationListener()
        else:
            self.listener = listener
        self.delegate = None

    def validateContainer(self, filename):
        lex = Container.identify(filename)
        resolver = data_store.MemoryDataStore(lex)

        with zip.ZipFile.NewZipFile(resolver, filename) as zip_file:
            if lex == lexicon.standard:
                self.delegate = InterimStdValidator(resolver, lex, self.listener)
            elif lex == lexicon.legacy:
                self.delegate = PreStdValidator(resolver, lex, self.listener)
            else:
                raise ValueError

            self.delegate.doValidateContainer()

    def validateContainerMultiPart(self, filenamea, filenameb):
        # in this simple example, we assume that both files passed are members of the Container
        lex = Container.identify(filenamea)
        resolver = data_store.MemoryDataStore(lex)

        with zip.ZipFile.NewZipFile(resolver, filenamea) as zip_filea:
            with zip.ZipFile.NewZipFile(resolver, filenameb) as zip_fileb:
                if lex == lexicon.standard:
                    self.delegate = InterimStdValidator(resolver, lex, self.listener)
                elif lex == lexicon.legacy:
                    self.delegate = PreStdValidator(resolver, lex, self.listener)
                else:
                    raise ValueError

                self.delegate.doValidateContainer()

    def validateBlockMapHash(self, mapStreamURI, imageStreamURI):

        storedHash = self.resolver.QuerySubjectPredicate(mapStreamURI,
                                                             self.lexicon.blockMapHash).next()
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

        storedBlockHashesHash = self.getStoredBlockHashes(str(imageStreamURI))

        storedBlockHashesHash = sorted(storedBlockHashesHash, cmp=hashLengthComparator)

        calculatedHash = hashes.new(storedHashDataType)
        for hash in storedBlockHashesHash:
            bytes = hash.digest()
            calculatedHash.update(bytes)

        bytes = self.resolver.QuerySubjectPredicate(mapStreamURI, self.lexicon.mapPointHash).next().digest()
        calculatedHash.update(bytes)

        bytes = self.resolver.QuerySubjectPredicate(mapStreamURI, self.lexicon.mapIdxHash).next().digest()
        # bytes = self.calculateMapIdxHash(mapStreamURI).digest()
        calculatedHash.update(bytes)

        try:
            bytes = self.resolver.QuerySubjectPredicate(mapStreamURI, self.lexicon.mapPathHash).next().digest()
            # bytes = self.calculateMapPathHash(mapStreamURI).digest()
            calculatedHash.update(bytes)
        except:
            pass

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

                    chunkIdx = offset / imageStream.chunk_size
                    storedBlockHash = imageStream.readBlockHash(chunkIdx, hashDataType)
                    if calculatedBlockHash != storedBlockHash.value:
                        self.listener.onInvalidBlockHash(calculatedBlockHash, storedBlockHash.value, imageStreamURI, offset)
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
        s =  self.resolver.QuerySubjectPredicate(imageStreamURI, self.lexicon.blockHashesHash)
        for h in s:
            blockHashAlgo = h.datatype
            digest = h.value
            digestDataType = h.datatype
            hash = BlockHashesHash(blockHashAlgo, digest, digestDataType)
            hashes.append(hash)
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

    def validateMapIdxHash(self, mapURI):
        storedHash = self.resolver.QuerySubjectPredicate(mapURI, self.lexicon.mapIdxHash).next()
        storedHashDataType = storedHash.datatype
        return self.validateSegmentHash(mapURI, "mapIdxHash", self.calculateMapIdxHash(mapURI, storedHashDataType))


    def calculateMapIdxHash(self, mapURI, hashDataType):
        return self.calculateSegmentHash(mapURI, "idx", hashDataType)


    def validateMapPointHash(self, mapURI):
        storedHashDataType = self.resolver.QuerySubjectPredicate(mapURI, self.lexicon.mapPointHash).next().datatype
        return self.validateSegmentHash(mapURI, "mapPointHash", self.calculateMapPointHash(mapURI, storedHashDataType))


    def calculateMapPointHash(self, mapURI, storedHashDataType):
        return self.calculateSegmentHash(mapURI, "map", storedHashDataType)


    def validateMapPathHash(self, mapURI):
        storedHashDataType = self.resolver.QuerySubjectPredicate(mapURI, self.lexicon.mapPathHash).next().datatype
        return self.validateSegmentHash(mapURI, "mapPathHash", self.calculateMapPathHash(mapURI, storedHashDataType))


    def calculateMapPathHash(self, mapURI, storedHashDataType):
        return self.calculateSegmentHash(mapURI, "mapPath", storedHashDataType)

    def calculateMapHash(self, mapURI, storedHashDataType):
        calculatedHash = hashes.new(storedHashDataType)

        calculatedHash.update(self.readSegment(mapURI, "map"))
        calculatedHash.update(self.readSegment(mapURI, "idx"))

        try:
            calculatedHash.update(self.readSegment(mapURI, "mapPath"))
        except:
            pass

        return hashes.newImmutableHash(calculatedHash.hexdigest(), storedHashDataType)

    def validateMapHash(self, mapURI):
        storedHashDataType = self.resolver.QuerySubjectPredicate(mapURI, self.lexicon.mapHash).next().datatype
        return self.validateSegmentHash(mapURI, "mapHash", self.calculateMapHash(mapURI, storedHashDataType))



    def validateSegmentHash(self, mapURI, hashType, calculatedHash):
        storedHash = self.resolver.QuerySubjectPredicate(mapURI, self.lexicon.base + hashType).next()

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

    def validateContainer(self, filename):
        with zip.ZipFile.NewZipFile(self.resolver, filename) as zip_file:
            self.doValidateContainer()

    # pre AFF4 standard Evimetry uses the contains relationship to locate the local
    # image stream of a Map
    def findLocalImageStreamOfMap(self, mapStreamURI):
        imageStreamURI = self.resolver.QuerySubjectPredicate(mapStreamURI,
                                                             self.lexicon.contains).next()
        return imageStreamURI

    def doValidateContainer(self):
        imageURI = self.resolver.QueryPredicateObject(lexicon.AFF4_TYPE, self.lexicon.Image).next()

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

        storedHash = self.resolver.QuerySubjectPredicate(mapStreamURI,
                                                             self.lexicon.blockHashesHash).next()
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

    def validateContainer(self, filename):
        with zip.ZipFile.NewZipFile(self.resolver, filename) as zip_file:
            self.doValidateContainer()

    def getParentMap(self, imageStreamURI):
        imageStreamVolume = self.resolver.QuerySubjectPredicate(imageStreamURI, self.lexicon.stored).next()

        for map in self.resolver.QuerySubjectPredicate(imageStreamURI, self.lexicon.target):
            mapVolume = self.resolver.QuerySubjectPredicate(map, self.lexicon.stored).next()
            if mapVolume == imageStreamVolume:
                return map

        raise Exception("Illegal State")

    def doValidateContainer(self):
        image = self.resolver.QueryPredicateObject(lexicon.AFF4_TYPE, self.lexicon.Image).next()
        datastreams = list(self.resolver.QuerySubjectPredicate(image, self.lexicon.dataStream))

        calculatedHashes = {}

        for stream in datastreams:
            if self.isMap(stream):
                for imageStreamURI in self.resolver.QuerySubjectPredicate(stream, self.lexicon.dependentStream):
                    parentMap = self.getParentMap(imageStreamURI)
                    if parentMap == stream:
                        # only validate the map and stream pair in the same container
                        self.validateBlockHashesHash(imageStreamURI)
                        self.validateMapIdxHash(parentMap)
                        self.validateMapPointHash(parentMap)
                        self.validateMapPathHash(parentMap)
                        self.validateMapHash(parentMap)

                        calculatedHash = self.validateBlockMapHash(parentMap, imageStreamURI)
                        calculatedHashes[parentMap] = calculatedHash

        storedHash = self.resolver.QuerySubjectPredicate(image, self.lexicon.hash).next()

        hasha = ""
        hashb = ""
        parentmap = None

        # TODO: handle more cleanlythe sematic difference between datatypes
        if len(calculatedHashes.keys()) == 1:
            # This is a single part image
            # The single AFF4 hash is just the blockMapHash

            parentMap = calculatedHashes.keys()[0]
            calculatedHash = calculatedHashes[parentMap]

            hasha = storedHash
            hashb = calculatedHash

        else:
            # This is a multiple part image
            # The single AFF4 hash is one layer up in the Merkel tree again, with the
            # subordinate nodes being the blockMapHashes for the map stored in each container volume

            # The hash algorithm we use for the single AFF4 hash is the same algorithm we
            # use for all of the Merkel tree inner nodes
            firstCalculatedHash = calculatedHashes[calculatedHashes.keys()[0]]
            currentHash = hashes.new(firstCalculatedHash.datatype)

            # We rely on the natural ordering of the map URN's as they are stored in the map
            # to order the blockMapHashes in the Merkel tree.
            for parentMap in calculatedHashes.keys():
                calculatedHash = calculatedHashes[parentMap]
                bytes = calculatedHash.digest()
                currentHash.update(bytes)

            hasha = storedHash.value
            hashb = currentHash.hexdigest()

        if hasha != hashb:
            self.listener.onInvalidHash("AFF4Hash", hasha, hashb, parentMap)
        else:
            self.listener.onValidHash("AFF4Hash", hasha, parentMap)



    def getStoredBlockHashes(self, imageStreamURI):
        res = []

        for blockHashURI in self.resolver.SelectSubjectsByPrefix(str(imageStreamURI) + "/blockhash."):
            hash = self.resolver.QuerySubjectPredicate(blockHashURI, self.lexicon.hash).next()
            trimOffset = len(str(imageStreamURI) + "/blockhash.")
            blockHashAlgoShortName = blockHashURI[trimOffset:]
            blockHashAlgoType = hashes.fromShortName(blockHashAlgoShortName)
            hash = BlockHashesHash(blockHashAlgoType, hash.value, hash.datatype)
            res.append(hash)

        return res

    def isMap(self, stream):
        types = self.resolver.QuerySubjectPredicate(stream, lexicon.AFF4_TYPE)
        if self.lexicon.map in types:
            return True
        return False