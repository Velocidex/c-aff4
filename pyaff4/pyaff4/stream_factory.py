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

from pyaff4.symbolic_streams import  *
from pyaff4 import rdfvalue
import re

class StreamFactory:
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
            stream =  ZeroStream(resolver=self.resolver, urn=urn)
            stream.symbol = 0
            return stream

        if urn == self.lexicon.base + "FF":
            stream =  RepeatedStream(resolver=self.resolver, urn=urn)
            stream.symbol = 255
            return stream

        if urn == self.lexicon.base + "UnknownData":
            stream =  RepeatedStringStream(resolver=self.resolver, urn=urn)
            stream.repeatedString = "UNKNOWN"
            return stream

        if urn.startswith(self.lexicon.base + "SymbolicStream") and len(urn) == len(self.lexicon.base + "SymbolicStream") + 2:
            shortName = urn[len(self.lexicon.base + "SymbolicStream"):].upper()
            value = shortName.decode('hex')
            stream = RepeatedStream(resolver=self.resolver, urn=urn)
            stream.symbol = value
            return stream

        if urn.startswith("http://afflib.org/2012/SymbolicStream#") and len(urn) == len("http://afflib.org/2012/SymbolicStream#") + 2:
            shortName = urn[len("http://afflib.org/2012/SymbolicStream#"):].upper()
            value = shortName.decode('hex')
            stream = RepeatedStream(resolver=self.resolver, urn=urn)
            stream.symbol = value
            return stream

        if urn.startswith(self.lexicon.base) and len(urn) == len(self.lexicon.base) + 2:
            shortName = urn[len(self.lexicon.base):].upper()
            value = shortName.decode('hex')
            stream = RepeatedStream(resolver=self.resolver, urn=urn)
            stream.symbol = value
            return stream

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
            stream =  ZeroStream(resolver=self.resolver, urn=urn)
            stream.symbol = 0
            return stream

        if urn == self.lexicon.base + "UnknownData":
            stream =  RepeatedStringStream(resolver=self.resolver, urn=urn)
            stream.repeatedString = "UNKNOWN"
            return stream

        if urn == self.lexicon.base + "UnreadableData":
            stream =  RepeatedStringStream(resolver=self.resolver, urn=urn)
            stream.repeatedString = "UNREADABLEDATA"
            return stream

        if urn.startswith(self.lexicon.base + "SymbolicStream") and len(urn) == len(self.lexicon.base + "SymbolicStream") + 2:
            shortName = urn[len(self.lexicon.base + "SymbolicStream"):].upper()
            value = shortName.decode('hex')
            stream = RepeatedStream(resolver=self.resolver, urn=urn)
            stream.symbol = value
            return stream

        raise ValueError