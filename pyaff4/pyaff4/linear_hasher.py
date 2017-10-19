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

from builtins import object
import hashlib
import rdflib

from pyaff4 import block_hasher
from pyaff4 import container
from pyaff4 import data_store
from pyaff4 import hashes
from pyaff4 import lexicon
from pyaff4 import rdfvalue
from pyaff4 import zip


class LinearHasher(object):
    def __init__(self, listener=None):
        if listener == None:
            self.listener = block_hasher.ValidationListener()
        else:
            self.listener = listener
        self.delegate = None

    def hash(self, urn, mapURI, hashDataType):
        lex = container.Container.identifyURN(urn)
        resolver = data_store.MemoryDataStore(lex)

        with zip.ZipFile.NewZipFile(resolver, urn) as zip_file:
            if lex == lexicon.standard:
                self.delegate = InterimStdLinearHasher(resolver, lex, self.listener)
            elif lex == lexicon.legacy:
                self.delegate = PreStdLinearHasher(resolver, lex, self.listener)
            elif lex == lexicon.scudette:
                self.delegate = ScudetteLinearHasher(resolver, lex, self.listener)
            else:
                raise ValueError

            return self.delegate.doHash(mapURI, hashDataType)

    def hashMulti(self, urna, urnb, mapURI, hashDataType):
        lex = container.Container.identifyURN(urna)
        resolver = data_store.MemoryDataStore(lex)

        with zip.ZipFile.NewZipFile(resolver, urna) as zip_filea:
            with zip.ZipFile.NewZipFile(resolver, urnb) as zip_fileb:
                if lex == lexicon.standard:
                    self.delegate = InterimStdLinearHasher(resolver, lex, self.listener)
                elif lex == lexicon.legacy:
                    self.delegate = PreStdLinearHasher(resolver, lex, self.listener)
                else:
                    raise ValueError

                return self.delegate.doHash(mapURI, hashDataType)

    def doHash(self, mapURI, hashDataType):
        hash = hashes.new(hashDataType)
        if not self.isMap(mapURI):
            import pdb; pdb.set_trace()

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

    def isMap(self, stream):
        for type in self.resolver.QuerySubjectPredicate(stream, lexicon.AFF4_TYPE):
            if self.lexicon.map == type:
                return True

        return False


class PreStdLinearHasher(LinearHasher):
    def __init__(self, resolver, lex, listener=None):
        LinearHasher.__init__(self, listener)
        self.lexicon = lex
        self.resolver = resolver


class InterimStdLinearHasher(LinearHasher):
    def __init__(self, resolver, lex, listener=None):
        LinearHasher.__init__(self, listener)
        self.lexicon = lex
        self.resolver = resolver


class ScudetteLinearHasher(LinearHasher):
    def __init__(self, resolver, lex, listener=None):
        LinearHasher.__init__(self, listener)
        self.lexicon = lex
        self.resolver = resolver
