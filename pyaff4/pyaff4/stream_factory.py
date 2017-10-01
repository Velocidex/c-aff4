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
from builtins import object
from pyaff4.symbolic_streams import  *
from pyaff4 import rdfvalue
import re

class StreamFactory(object):
    def __init__(self, resolver, lex):
        self.lexicon = lex
        self.resolver = resolver
        self.symbolmatcher = re.compile("[0-9A-F]{2}")
        self.fixedSymbolics = [ self.lexicon.base + "Zero",
                                self.lexicon.base + "UnknownData",
                                self.lexicon.base + "UnreadableData",
                                self.lexicon.base + "NoData"]

# TODO: Refactor the below classes to split the subname from the NS
# then do matching only on the subnname

class PreStdStreamFactory(StreamFactory):
    def __init__(self, resolver, lex):
        StreamFactory.__init__(self, resolver, lex)
        self.fixedSymbolics.append(self.lexicon.base + "FF")

    def isSymbolicStream(self, urn):
        if type(urn) == rdfvalue.URN:
            urn = str(urn)
        if not urn.startswith("http://"):
            return False
        else:
            if urn in self.fixedSymbolics:
                return True

            # Pre-Std Evimetry Symbolic Streams are of the form
            # http://afflib.org/2009#FF
            if urn.startswith(self.lexicon.base) and len(urn) == len(self.lexicon.base) + 2:
                # now verify symbolic part
                shortName = urn[len(self.lexicon.base):].upper()

                if self.symbolmatcher.match(shortName) != None:
                    return True

            if urn.startswith(self.lexicon.base + "SymbolicStream"):
                return True

            if urn.startswith("http://afflib.org/2012/SymbolicStream#"):
                return True

            return False

    def createSymbolic(self, urn):
        if type(urn) == rdfvalue.URN:
            urn = str(urn)

        if urn == self.lexicon.base + "Zero":
            return RepeatedStream(resolver=self.resolver, urn=urn,
                                  symbol=b"\x00")

        if urn == self.lexicon.base + "FF":
            return RepeatedStream(resolver=self.resolver, urn=urn,
                                  symbol=b"\xff")

        if urn == self.lexicon.base + "UnknownData":
            return RepeatedStringStream(resolver=self.resolver, urn=urn,
                                        repeated_string=GetUnknownString())

        if (urn.startswith(self.lexicon.base + "SymbolicStream") and
            len(urn) == len(self.lexicon.base + "SymbolicStream") + 2):
            shortName = urn[len(self.lexicon.base + "SymbolicStream"):].upper()
            value = binascii.unhexlify(shortName)
            return RepeatedStream(resolver=self.resolver, urn=urn, symbol=value)

        if (urn.startswith("http://afflib.org/2012/SymbolicStream#") and
            len(urn) == len("http://afflib.org/2012/SymbolicStream#") + 2):
            shortName = urn[len("http://afflib.org/2012/SymbolicStream#"):].upper()
            value = binascii.unhexlify(shortName)
            return RepeatedStream(resolver=self.resolver, urn=urn,
                                    symbol=value)

        if urn.startswith(self.lexicon.base) and len(urn) == len(self.lexicon.base) + 2:
            shortName = urn[len(self.lexicon.base):].upper()
            value = binascii.unhexlify(shortName)
            return RepeatedStream(resolver=self.resolver, urn=urn,
                                    symbol=value)

        raise ValueError


class StdStreamFactory(StreamFactory):

    def isSymbolicStream(self, urn):
        if type(urn) == rdfvalue.URN:
            urn = str(urn)
        if not urn.startswith("http://"):
            return False
        else:
            if urn in self.fixedSymbolics:
                return True

            if urn.startswith(self.lexicon.base + "SymbolicStream"):
                return True

            return False

    def createSymbolic(self, urn):
        if type(urn) == rdfvalue.URN:
            urn = str(urn)

        if urn == self.lexicon.base + "Zero":
            return RepeatedStream(resolver=self.resolver, urn=urn,
                                  symbol=b"\x00")

        if urn == self.lexicon.base + "UnknownData":
            return RepeatedStringStream(
                resolver=self.resolver, urn=urn,
                repeated_string=GetUnknownString())

        if urn == self.lexicon.base + "UnreadableData":
            return RepeatedStringStream(
                resolver=self.resolver, urn=urn,
                repeated_string=GetUnreadableString())

        if (urn.startswith(self.lexicon.base + "SymbolicStream") and
            len(urn) == len(self.lexicon.base + "SymbolicStream") + 2):
            shortName = urn[len(self.lexicon.base + "SymbolicStream"):].upper()
            value = binascii.unhexlify(shortName)
            return RepeatedStream(resolver=self.resolver, urn=urn,
                                    symbol=value)

        raise ValueError


def _MakeTile(repeated_string):
    """Make exactly 1Mb tile of the repeated string."""
    total_size = 1024*1024
    tile = repeated_string * (total_size // len(repeated_string))
    tile += repeated_string[:total_size % len(repeated_string)]
    return tile

# Exactly 1Mb.
_UNKNOWN_STRING = None
def GetUnknownString():
    global _UNKNOWN_STRING
    if _UNKNOWN_STRING is not None:
        return _UNKNOWN_STRING

    _UNKNOWN_STRING = _MakeTile(b"UNKNOWN")
    return _UNKNOWN_STRING


# Exactly 1Mb.
_UNREADABLE_STRING = None
def GetUnreadableString():
    global _UNREADABLE_STRING
    if _UNREADABLE_STRING is not None:
        return _UNREADABLE_STRING

    _UNREADABLE_STRING = _MakeTile(b"UNREADABLEDATA")
    return _UNREADABLE_STRING
