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

from pyaff4.block_hasher import ValidationListener

from pyaff4 import data_store
from pyaff4 import hashes
from pyaff4 import lexicon
from pyaff4 import rdfvalue
import rdflib
import zip
import hashlib


class LinearHasher:
    def __init__(self, resolver, aff4NSprefix, listener=None):
        self.resolver = resolver
        if listener == None:
            self.listener = ValidationListener()
        else:
            self.listener = listener
        self.delegate = None
        self.affNS = aff4NSprefix

    def hash(self, filename, mapURI, hashDataType):
        with zip.ZipFile.NewZipFile(self.resolver, filename) as zip_file:
            try:
                version = zip_file.OpenZipSegment("version.txt")
                self.delegate = InterimStdLinearHasher(self.resolver, self.affNS, self.listener)
            except:
                self.delegate = PreStdLinearHasher(self.resolver, self.affNS, self.listener)

            return self.delegate.doHash(mapURI, hashDataType)

    def hashMulti(self, filenamea, filenameb, mapURI, hashDataType):
        with zip.ZipFile.NewZipFile(self.resolver, filenamea) as zip_filea:
            with zip.ZipFile.NewZipFile(self.resolver, filenameb) as zip_fileb:
                try:
                    version = zip_filea.OpenZipSegment("version.txt")
                    self.delegate = InterimStdLinearHasher(self.resolver, self.affNS, self.listener)
                except:
                    self.delegate = PreStdLinearHasher(self.resolver, self.affNS, self.listener)

                return self.delegate.doHash(mapURI, hashDataType)



    def doHash(self, mapURI, hashDataType):
        hash = hashes.new(hashDataType)
        if self.isMap(mapURI):
            with self.resolver.AFF4FactoryOpen(mapURI) as mapStream:
                remaining = mapStream.Size()
                count = 0
                while remaining > 0:
                    toRead = min(32*1024, remaining)
                    data = mapStream.Read(toRead)
                    assert len(data) == toRead
                    remaining -= len(data)
                    hash.update(data)
                    count = count + 1

                b = hash.hexdigest()
                return hashes.newImmutableHash(b, hashDataType)
        raise Exception("IllegalState")

class PreStdLinearHasher(LinearHasher):
    def __init__(self, resolver, aff4NSprefix, listener=None):
        LinearHasher.__init__(self, resolver, aff4NSprefix, listener)

    def isMap(self, stream):
        types = self.resolver.QuerySubjectPredicate(stream, lexicon.AFF4_TYPE)
        if self.affNS + "map" in types:
            return True

        return False

class InterimStdLinearHasher(LinearHasher):
    def __init__(self, resolver, aff4NSprefix, listener=None):
        LinearHasher.__init__(self, resolver, aff4NSprefix, listener)

    def isMap(self, stream):
        types = self.resolver.QuerySubjectPredicate(stream, lexicon.AFF4_TYPE)
        if self.affNS + "Map" in types:
            return True

        return False