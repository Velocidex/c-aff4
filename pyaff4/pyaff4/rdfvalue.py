# Copyright 2014 Google Inc. All rights reserved.
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

"""RDF Values are responsible for serialization."""
from __future__ import unicode_literals
from future import standard_library
standard_library.install_aliases()
from builtins import str
from builtins import object
import functools
import urllib.parse
import urllib.request, urllib.parse, urllib.error

import binascii
import posixpath
import rdflib

from pyaff4 import registry
from pyaff4 import utils

# pylint: disable=protected-access


class Memoize(object):
    def __call__(self, f):
        f.memo_pad = {}

        @functools.wraps(f)
        def Wrapped(self, *args):
            key = tuple(args)
            if len(f.memo_pad) > 100:
                f.memo_pad.clear()

            if key not in f.memo_pad:
                f.memo_pad[key] = f(self, *args)

            return f.memo_pad[key]

        return Wrapped


class RDFValue(object):
    datatype = ""

    def __init__(self, initializer=None):
        self.Set(initializer)

    def GetRaptorTerm(self):
        return rdflib.Literal(self.SerializeToString(),
                              datatype=self.datatype)

    def SerializeToString(self):
        """Serializes to a sequence of bytes."""
        return ""

    def UnSerializeFromString(self, string):
        """Unserializes from bytes."""
        raise NotImplementedError

    def Set(self, string):
        raise NotImplementedError

    def __bytes__(self):
        return self.SerializeToString()

    def __eq__(self, other):
        return utils.SmartStr(self) == utils.SmartStr(other)

    def __req__(self, other):
        return utils.SmartStr(self) == utils.SmartStr(other)

    def __hash__(self):
        return hash(self.SerializeToString())


class RDFBytes(RDFValue):
    value = b""
    datatype = rdflib.XSD.hexBinary

    def SerializeToString(self):
        return binascii.hexlify(self.value)

    def UnSerializeFromString(self, string):
        self.Set(binascii.unhexlify(string))

    def Set(self, data):
        self.value = data

    def __eq__(self, other):
        if isinstance(other, RDFBytes):
            return self.value == other.value


class XSDString(RDFValue):
    """A unicode string."""
    datatype = rdflib.XSD.string

    def SerializeToString(self):
        return utils.SmartStr(self.value)

    def UnSerializeFromString(self, string):
        self.Set(utils.SmartUnicode(string))

    def Set(self, data):
        self.value = utils.SmartUnicode(data)

    def __str__(self):
        return self.value


@functools.total_ordering
class XSDInteger(RDFValue):
    datatype = rdflib.XSD.integer

    def SerializeToString(self):
        return utils.SmartStr(self.value)

    def UnSerializeFromString(self, string):
        self.Set(int(string))

    def Set(self, data):
        self.value = int(data)

    def __eq__(self, other):
        if isinstance(other, XSDInteger):
            return self.value == other.value
        return self.value == other

    def __int__(self):
        return self.value

    def __long__(self):
        return int(self.value)

    def __cmp__(self, o):
        return self.value - o

    def __add__(self, o):
        return self.value + o

    def __lt__(self, o):
        return self.value < o

    def __str__(self):
        return str(self.value)


class RDFHash(XSDString):
    # value is the hex encoded digest.

    def __eq__(self, other):
        if isinstance(other, RDFHash):
            if self.datatype == other.datatype:
                return self.value == other.value
        return utils.SmartStr(self.value) == utils.SmartStr(other)

    def __ne__(self, other):
        return not self == other

    def digest(self):
        return binascii.unhexlify(self.value)


class SHA512Hash(RDFHash):
    datatype = rdflib.URIRef("http://aff4.org/Schema#SHA512")


class SHA256Hash(RDFHash):
    datatype = rdflib.URIRef("http://aff4.org/Schema#SHA256")


class SHA1Hash(RDFHash):
    datatype = rdflib.URIRef("http://aff4.org/Schema#SHA1")


class Blake2bHash(RDFHash):
    datatype = rdflib.URIRef("http://aff4.org/Schema#Blake2b")


class MD5Hash(RDFHash):
    datatype = rdflib.URIRef("http://aff4.org/Schema#MD5")


class SHA512BlockMapHash(RDFHash):
    datatype = rdflib.URIRef("http://aff4.org/Schema#blockMapHashSHA512")



class URN(RDFValue):
    """Represent a URN.

    According to RFC1738 URLs must be encoded in ASCII. Therefore the
    internal representation of a URN is bytes. When creating the URN
    from other forms (e.g. filenames, we assume UTF8 encoding if the
    filename is a unicode string.
    """

    # The encoded URN as a unicode string.
    value = None

    original_filename = None

    @classmethod
    def FromFileName(cls, filename):
        """Parse the URN from filename.

        Filename may be a unicode string, in which case it will be
        UTF8 encoded into the URN. URNs are always ASCII.
        """
        result = cls("file:%s" % urllib.request.pathname2url(filename))
        result.original_filename = filename
        return result

    @classmethod
    def NewURNFromFilename(cls, filename):
        return cls.FromFileName(filename)

    def ToFilename(self):
        # For file: urls we exactly reverse the conversion applied in
        # FromFileName.
        if self.value.startswith("file:"):
            return urllib.request.url2pathname(self.value[5:])

        components = self.Parse()
        if components.scheme == "file":
            return components.path

    def GetRaptorTerm(self):
        return rdflib.URIRef(self.value)

    def SerializeToString(self):
        components = self.Parse()
        return utils.SmartStr(urllib.parse.urlunparse(components))

    def UnSerializeFromString(self, string):
        utils.AssertStr(string)
        self.Set(utils.SmartUnicode(string))
        return self

    def Set(self, data):
        if data is None:
            return

        elif isinstance(data, URN):
            self.value = data.value
        else:
            utils.AssertUnicode(data)
            self.value = data

    def Parse(self):
        return self._Parse(self.value)

    # URL parsing seems to be slow in Python so we cache it as much as possible.
    @Memoize()
    def _Parse(self, value):
        components = urllib.parse.urlparse(value)

        # dont normalise path for http URI's
        if components.scheme and not components.scheme == "http":
            normalized_path = posixpath.normpath(components.path)
            if normalized_path == ".":
                normalized_path = ""

            components = components._replace(path=normalized_path)
        if not components.scheme:
            # For file:// URNs, we need to parse them from a filename.
            components = components._replace(
                netloc="",
                path=urllib.request.pathname2url(value),
                scheme="file")
            self.original_filename = value

        return components

    def Scheme(self):
        components = self.Parse()
        return components.scheme

    def Append(self, component, quote=True):
        components = self.Parse()
        if quote:
            component = urllib.parse.quote(component)

        # Work around usual posixpath.join bug.
        component = component.lstrip("/")
        new_path = posixpath.normpath(posixpath.join(
            "/", components.path, component))

        components = components._replace(path=new_path)

        return URN(urllib.parse.urlunparse(components))

    def RelativePath(self, urn):
        urn_value = str(urn)
        if urn_value.startswith(self.value):
            return urn_value[len(self.value):]

    def __str__(self):
        return self.value

    def __lt__(self, other):
        return self.value < utils.SmartUnicode(other)

    def __repr__(self):
        return "<%s>" % self.value


def AssertURN(urn):
    if not isinstance(urn, URN):
        raise TypeError("Expecting a URN.")


def AssertURN(urn):
    if not isinstance(urn, URN):
        raise TypeError("Expecting a URN.")


registry.RDF_TYPE_MAP.update({
    rdflib.XSD.hexBinary: RDFBytes,
    rdflib.XSD.string: XSDString,
    rdflib.XSD.integer: XSDInteger,
    rdflib.XSD.int: XSDInteger,
    rdflib.XSD.long: XSDInteger,
    rdflib.URIRef("http://aff4.org/Schema#SHA512"): SHA512Hash,
    rdflib.URIRef("http://aff4.org/Schema#SHA256"): SHA256Hash,
    rdflib.URIRef("http://aff4.org/Schema#SHA1"): SHA1Hash,
    rdflib.URIRef("http://aff4.org/Schema#MD5"): MD5Hash,
    rdflib.URIRef("http://aff4.org/Schema#Blake2b"): Blake2bHash,
    rdflib.URIRef("http://aff4.org/Schema#blockMapHashSHA512"): SHA512BlockMapHash,
    rdflib.URIRef("http://afflib.org/2009/aff4#SHA512"): SHA512Hash,
    rdflib.URIRef("http://afflib.org/2009/aff4#SHA256"): SHA256Hash,
    rdflib.URIRef("http://afflib.org/2009/aff4#SHA1"): SHA1Hash,
    rdflib.URIRef("http://afflib.org/2009/aff4#MD5"): MD5Hash,
    rdflib.URIRef("http://afflib.org/2009/aff4#blockMapHashSHA512"): SHA512BlockMapHash
})
