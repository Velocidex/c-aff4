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

"""An implementation of the ZipFile based AFF4 volume."""

import logging
import urlparse
import re
import string
import zipfile
import StringIO

from pyaff4 import aff4
from pyaff4 import lexicon
from pyaff4 import rdfvalue
from pyaff4 import registry

LOGGER = logging.getLogger("pyaff4")


class FileBackedObject(aff4.AFF4Stream):
    def LoadFromURN(self):
        components = self.urn.Parse()
        if components.scheme != "file":
            raise TypeError("Only file:// URNs are supported.")

        mode = (self.resolver.Get(self.urn, lexicon.AFF4_STREAM_WRITE_MODE)
                or "read")
        if mode == "truncate":
            flags = "w+b"
        elif mode == "append":
            flags = "a+b"
        else:
            flags = "rb"

        filename = self.urn.ToFilename()
        self.fd = open(filename, flags)

    def Read(self, length):
        self.fd.seek(self.readptr)
        result = self.fd.read(length)
        self.readptr += len(result)
        return result

    def Write(self, data):
        self.MarkDirty()

        self.fd.seek(self.readptr)
        self.fd.write(data)
        self.fd.flush()
        self.readptr += len(data)

    def Flush(self):
        if self.IsDirty():
            self.fd.flush()
        super(FileBackedObject, self).Flush()

    def Prepare(self):
        self.readptr = 0

    def Truncate(self):
        self.fd.truncate(0)

    def Size(self):
        self.fd.seek(0, 2)
        return self.fd.tell()


registry.AFF4_TYPE_MAP["file"] = FileBackedObject


class ZipFileSegment(FileBackedObject):
    def LoadFromURN(self):
        owner_urn = self.resolver.Get(self.urn, lexicon.AFF4_STORED)
        with self.resolver.AFF4FactoryOpen(owner_urn) as owner:
            self.LoadFromZipFile(owner)

    def LoadFromZipFile(self, owner):
        """Read the segment data from the ZipFile owner."""
        member_name = owner.member_name_for_urn(self.urn)
        try:
            # AFF4 Segments are supposed to be small - so we can just
            # read them all into memory at once.
            self.fd = StringIO.StringIO(owner.zip_handle.read(member_name))

        except KeyError:
            self.fd = StringIO.StringIO("")

    def Flush(self):
        if self.IsDirty():
            owner_urn = self.resolver.Get(self.urn, lexicon.AFF4_STORED)
            with self.resolver.AFF4FactoryOpen(owner_urn) as owner:
                member_name = owner.member_name_for_urn(self.urn)

                # Write all new segments at the end of the file.
                owner.zip_handle.fp.Seek(0, 2)
                owner.zip_handle.writestr(member_name, self.fd.getvalue())

        super(ZipFileSegment, self).Flush()


class ZipFile(aff4.AFF4Volume):
    def __init__(self, *args, **kwargs):
        super(ZipFile, self).__init__(*args, **kwargs)
        self.children = set()
        self.printables = set(string.printable)
        for i in "!$\\:*%?\"<>|]":
            self.printables.discard(i)

    def member_name_for_urn(self, member_urn):
        filename = self.urn.RelativePath(member_urn)
        if filename.startswith("/"):
            filename = filename[1:]

        # Escape chars which are non printable.
        escaped_filename = []
        for c in filename:
            if c in self.printables:
                escaped_filename.append(c)
            else:
                escaped_filename.append("%%%02x" % ord(c))

        return "".join(escaped_filename)

    def urn_from_member_name(self, member):
        # Remove %xx escapes.
        member = re.sub(
            "%(..)", lambda x: chr(int("0x" + x.group(1), 0)),
            member)
        if urlparse.urlparse(member).scheme == "aff4":
            return member

        return self.urn.Append(member, quote=False)

    @staticmethod
    def NewZipFile(resolver, backing_store_urn):
        result = ZipFile(resolver, urn=None)

        resolver.Set(result.urn, lexicon.AFF4_TYPE,
                     rdfvalue.URN(lexicon.AFF4_ZIP_TYPE))

        resolver.Set(result.urn, lexicon.AFF4_STORED,
                     rdfvalue.URN(backing_store_urn))

        return resolver.AFF4FactoryOpen(result.urn)

    def CreateMember(self, child_urn):
        member_filename = self.member_name_for_urn(child_urn)
        return self.CreateZipSegment(member_filename)

    def CreateZipSegment(self, filename):
        self.MarkDirty()

        segment_urn = self.urn_from_member_name(filename)

        # Is it in the cache?
        res = self.resolver.CacheGet(segment_urn)
        if res:
            return res

        self.resolver.Set(
            segment_urn, lexicon.AFF4_TYPE,
            rdfvalue.URN(lexicon.AFF4_ZIP_SEGMENT_TYPE))

        self.resolver.Set(segment_urn, lexicon.AFF4_STORED, self.urn)

        #  Keep track of all the segments we issue.
        self.children.add(segment_urn)

        result = ZipFileSegment(resolver=self.resolver, urn=segment_urn)
        result.LoadFromZipFile(self)

        LOGGER.info("Creating ZipFileSegment %s",
                    result.urn.SerializeToString())

        # Add the new object to the object cache.
        return self.resolver.CachePut(result)

    def OpenZipSegment(self, filename):
        raise NotImplementedError

    def LoadFromURN(self):
        backing_store_urn = self.resolver.Get(self.urn, lexicon.AFF4_STORED)
        if not backing_store_urn:
            raise IOError("Unable to load backing urn.")

        with self.resolver.AFF4FactoryOpen(backing_store_urn) as backing_store:
            mode = "a"
            write_mode = self.resolver.Get(
                backing_store_urn, lexicon.AFF4_STREAM_WRITE_MODE) or "read"

            if write_mode == "read":
                mode = "r"

            try:
                self.zip_handle = zipfile.ZipFile(backing_store, mode=mode,
                                                  allowZip64=True)
            except zipfile.BadZipfile:
                raise IOError("Unable to open zip file.")

            # The current URN is stored in the zip comment.
            if self.zip_handle.comment:
                self.urn.Set(self.zip_handle.comment)
                self.resolver.Set(self.urn, lexicon.AFF4_STORED,
                                  backing_store_urn)

            # Populate the resolver with the information from the zip handle.
            for name in self.zip_handle.namelist():
                segment_urn = self.urn_from_member_name(name)
                self.resolver.Set(segment_urn, lexicon.AFF4_TYPE,
                                  rdfvalue.URN(lexicon.AFF4_ZIP_SEGMENT_TYPE))

                self.resolver.Set(segment_urn, lexicon.AFF4_STORED,
                                  self.urn)

            try:
                turtle_data = self.zip_handle.read("information.turtle")
                self.resolver.LoadFromTurtle(turtle_data)
            except KeyError:
                pass

    def Flush(self):
        if self.IsDirty():
            while len(self.children):
                for child in list(self.children):
                    with self.resolver.CacheGet(child) as obj:
                        obj.Flush()

                    self.children.remove(child)

            # Add the turtle file to the volume.
            with self.CreateZipSegment("information.turtle") as turtle_segment:
                turtle_segment.Write(self.resolver.DumpToTurtle())
                turtle_segment.Flush()

            self.zip_handle.comment = self.urn.SerializeToString()
            # Put the EOCD at the end of the file.
            self.zip_handle.fp.Seek(0, 2)

            self.zip_handle.close()

        super(ZipFile, self).Flush()


registry.AFF4_TYPE_MAP[lexicon.AFF4_ZIP_TYPE] = ZipFile
registry.AFF4_TYPE_MAP[lexicon.AFF4_ZIP_SEGMENT_TYPE] = ZipFileSegment
