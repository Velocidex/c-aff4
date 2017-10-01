from __future__ import division
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

from builtins import str
from past.utils import old_div
from pyaff4 import aff4
from pyaff4 import utils
import sys
import binascii
import math

class RepeatedStream(aff4.AFF4Stream):

    def __init__(self, resolver=None, urn=None, symbol=b"\x00"):
        super(RepeatedStream, self).__init__(
            resolver=resolver, urn=urn)
        self.symbol = symbol

    def Read(self, length):
        return self.symbol * length

    def Write(self, data):
        raise NotImplementedError()

    def WriteStream(self, source):
        raise NotImplementedError()

    def Tell(self):
        return self.readptr

    def Size(self):
        return sys.maxsize

    def read(self, length=1024*1024):
        return self.Read(length)

    def seek(self, offset, whence=0):
        self.Seek(offset, whence=whence)

    def write(self, data):
        self.Write(data)

    def tell(self):
        return self.Tell()

    def flush(self):
        self.Flush()

    def Prepare(self):
        self.Seek(0)


class RepeatedStringStream(aff4.AFF4Stream):

    def __init__(self, resolver=None, urn=None, repeated_string=None):
        super(RepeatedStringStream, self).__init__(
            resolver=resolver, urn=urn)

        self.tile = repeated_string
        self.tilesize = len(self.tile)

    def Read(self, length):
        toRead = length
        res = b""
        while toRead > 0:
            offsetInTile = self.readptr % self.tilesize
            chunk = self.tile[offsetInTile : offsetInTile + toRead]
            res += chunk
            toRead -= len(chunk)
            self.readptr += len(chunk)

        return res

    def Write(self, data):
        raise NotImplementedError()

    def WriteStream(self, source):
        raise NotImplementedError()

    def Tell(self):
        return self.readptr

    def Size(self):
        return sys.maxsize

    def read(self, length=1024*1024):
        return self.Read(length)

    def seek(self, offset, whence=0):
        self.Seek(offset, whence=whence)

    def write(self, data):
        self.Write(data)

    def tell(self):
        return self.Tell()

    def flush(self):
        self.Flush()

    def Prepare(self):
        self.Seek(0)
