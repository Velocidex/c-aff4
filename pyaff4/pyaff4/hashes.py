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

from pyaff4 import lexicon
from pyaff4.rdfvalue import *
import hashlib
import pyblake2

def new(datatype):
    return map[datatype]()

def newImmutableHash(value, datatype):

    if datatype == lexicon.HASH_SHA1:
        h = SHA1Hash()
    elif datatype == lexicon.HASH_MD5:
        h = MD5Hash()
    elif datatype == lexicon.HASH_SHA512:
        h = SHA512Hash()
    elif datatype == lexicon.HASH_SHA256:
        h = SHA256Hash()
    elif datatype == lexicon.HASH_BLAKE2B:
        h = Blake2bHash()
    elif datatype == lexicon.HASH_BLOCKMAPHASH_SHA512:
        h = SHA512BlockMapHash()
    else:
        raise Exception
    h.Set(value)
    return h

def toShortAlgoName(datatype):
    return map[datatype]().name

def fromShortName(name):
    return nameMap[name]

def length(datatype):
    return map[datatype]().digest_size

map = {
    lexicon.HASH_SHA1: hashlib.sha1,
    lexicon.HASH_SHA256: hashlib.sha256,
    lexicon.HASH_SHA512: hashlib.sha512,
    lexicon.HASH_MD5: hashlib.md5,
    lexicon.HASH_BLAKE2B: pyblake2.blake2b
}

nameMap = {
    "md5" : lexicon.HASH_MD5,
    "sha1" : lexicon.HASH_SHA1,
    "sha256" : lexicon.HASH_SHA256,
    "sha512" : lexicon.HASH_SHA512,
    "blake2b" : lexicon.HASH_BLAKE2B,
    "blockMapHashSHA512" : lexicon.HASH_BLOCKMAPHASH_SHA512
}