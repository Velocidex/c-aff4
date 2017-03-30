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

from aff4 import AFF4Stream
import sys
import binascii
import math

class RepeatedStream(AFF4Stream):

    def __init__(self, *args, **kwargs):
        super(RepeatedStream, self).__init__(*args, **kwargs)

    def Read(self, length):
        res = bytearray([self.symbol] * length)
        return str(res)

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

class ZeroStream(RepeatedStream):
    def __init__(self, *args, **kwargs):
        super(ZeroStream, self).__init__(*args, **kwargs)

    def Read(self, length):
        res = bytearray(length)
        return str(res)


class RepeatedStringStream(AFF4Stream):

    def __init__(self, *args, **kwargs):
        super(RepeatedStringStream, self).__init__(*args, **kwargs)

        # the tile is a fixed size block of the repeated string.
        self.tile = None
        self.tilesize = 1024*1024
        self.repeatedString = None

    def initializeTile(self):
        countRepetitions = math.ceil(self.tilesize / len(self.repeatedString))
        repeatedBytes = bytearray(self.repeatedString, "ascii")
        while len(repeatedBytes) < self.tilesize:
            repeatedBytes = repeatedBytes + repeatedBytes

        self.tile = repeatedBytes[0:self.tilesize]

    def Read(self, length):
        if self.tile == None:
            self.initializeTile()

        toRead = length
        res = ""
        while toRead > 0:
            offsetInTile = self.readptr % self.tilesize
            currentLength = min(self.tilesize - offsetInTile, toRead)
            chunk = bytearray(self.tile)[offsetInTile:offsetInTile+currentLength]
            res += chunk
            toRead -= currentLength
            self.readptr += currentLength

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