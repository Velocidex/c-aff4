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
import functools
import urlparse
import urllib

import posixpath
import rdflib

from pyaff4 import registry

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
        return ""

    def UnSerializeFromString(self, string):
        raise NotImplementedError

    def Set(self, string):
        raise NotImplementedError

    def __str__(self):
        return self.SerializeToString()

    def __eq__(self, other):
        return str(self) == str(other)

    def __hash__(self):
        return hash(self.SerializeToString())


class RDFBytes(RDFValue):
    value = ""
    datatype = rdflib.XSD.hexBinary

    def SerializeToString(self):
        return self.value.encode("hex")

    def UnSerializeFromString(self, string):
        self.Set(string.decode("hex"))

    def Set(self, data):
        self.value = data

    def __eq__(self, other):
        if isinstance(other, RDFBytes):
            return self.value == other.value


class XSDString(RDFValue):
    datatype = rdflib.XSD.string

    def SerializeToString(self):
        return self.value.encode("utf8")

    def UnSerializeFromString(self, string):
        self.Set(string.decode("utf8"))

    def Set(self, data):
        self.value = unicode(data)

    def __unicode__(self):
        return unicode(self.value)


class XSDInteger(RDFValue):
    datatype = rdflib.XSD.integer

    def SerializeToString(self):
        return str(self.value)

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


class URN(RDFValue):

    original_filename = None

    @classmethod
    def FromFileName(cls, filename):
        result = cls("file:" + urllib.pathname2url(filename))
        result.original_filename = filename
        return result

    @classmethod
    def NewURNFromFilename(cls, filename):
        return cls.FromFileName(filename)

    def ToFilename(self):
        # For file: urls we exactly reverse the conversion applied in
        # FromFileName.
        if self.value.startswith("file:"):
            return urllib.url2pathname(self.value[5:])

        components = self.Parse()
        if components.scheme == "file":
            return components.path

    def GetRaptorTerm(self):
        return rdflib.URIRef(self.SerializeToString())

    def SerializeToString(self):
        components = self.Parse()
        return urlparse.urlunparse(components)

    def UnSerializeFromString(self, string):
        self.Set(int(string))

    def Set(self, data):
        if isinstance(data, URN):
            self.value = data.value
        else:
            self.value = str(data)

    def Parse(self):
        return self._Parse(self.value)

    # URL parsing seems to be slow in Python so we cache it as much as possible.
    @Memoize()
    def _Parse(self, value):
        components = urlparse.urlparse(value)
        normalized_path = posixpath.normpath(components.path)
        if normalized_path == ".":
            normalized_path = ""

        components = components._replace(path=normalized_path)
        if not components.scheme:
            # For file:// URNs, we need to parse them from a filename.
            components = components._replace(
                netloc="",
                path=urllib.pathname2url(value),
                scheme="file")
            self.original_filename = value

        return components

    def Scheme(self):
        components = self.Parse()
        return components.scheme

    def Append(self, component, quote=True):
        components = self.Parse()
        if quote:
            component = urllib.quote(component)

        # Work around usual posixpath.join bug.
        component = component.lstrip("/")
        new_path = posixpath.normpath(posixpath.join(
            components.path, component))

        components = components._replace(path=new_path)

        return URN(urlparse.urlunparse(components))

    def RelativePath(self, urn):
        value = self.SerializeToString()
        urn_value = str(urn)
        if urn_value.startswith(value):
            return urn_value[len(value):]

    def __repr__(self):
        return "<%s>" % self.SerializeToString()


registry.RDF_TYPE_MAP.update({
    rdflib.XSD.hexBinary: RDFBytes,
    rdflib.XSD.string: XSDString,
    rdflib.XSD.integer: XSDInteger,
    rdflib.XSD.int: XSDInteger,
    rdflib.XSD.long: XSDInteger,
    })
