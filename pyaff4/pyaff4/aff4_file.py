# Copyright 2015 Google Inc. All rights reserved.
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

"""An implementation of AFF4 file backed objects."""
import logging
import os
import StringIO

from pyaff4 import aff4
from pyaff4 import aff4_utils
from pyaff4 import lexicon
from pyaff4 import rdfvalue
from pyaff4 import registry

BUFF_SIZE = 64 * 1024


LOGGER = logging.getLogger("pyaff4")


class FileBackedObject(aff4.AFF4Stream):
    def _GetFilename(self):
        filename = self.resolver.Get(self.urn, lexicon.AFF4_FILE_NAME)
        if filename:
            return filename

        # Only file:// URNs are supported.
        if self.urn.Scheme() == "file":
            return self.urn.ToFilename()

    @staticmethod
    def _CreateIntermediateDirectories(components):
        """Recursively create intermediate directories."""
        path = os.sep

        if aff4.WIN32:
            # On windows we do not want a leading \ (e.g. C:\windows not
            # \C:\Windows)
            path = ""

        for component in components:
            path = path + component + os.sep
            LOGGER.info("Creating intermediate directories %s", path)

            if os.isdir(path):
                continue

            # Directory does not exist - Try to make it.
            try:
                aff4_utils.MkDir(path)
                continue
            except IOError as e:
                LOGGER.error(
                    "Unable to create intermediate directory: %s", e)
                raise

    def LoadFromURN(self):
        flags = "rb"

        filename = self._GetFilename()
        if not filename:
            raise IOError("Unable to find storage for %s" % self.urn)

        filename = unicode(filename)

        directory_components = os.sep.split(filename)
        directory_components.pop(-1)

        mode = self.resolver.Get(self.urn, lexicon.AFF4_STREAM_WRITE_MODE)
        if mode == "truncate":
            flags = "w+b"
            self.resolver.Set(self.urn, lexicon.AFF4_STREAM_WRITE_MODE,
                              rdfvalue.XSDString("append"))
            self.properties.writable = True
            self._CreateIntermediateDirectories(directory_components)

        elif mode == "append":
            flags = "a+b"
            self.properties.writable = True
            self._CreateIntermediateDirectories(directory_components)

        LOGGER.info("Opening file %s", filename)
        self.fd = open(filename, flags)
        try:
            self.fd.seek(0, 2)
            self.size = self.fd.tell()
        except IOError:
            self.properties.sizeable = False
            self.properties.seekable = False

    def Read(self, length):
        if self.fd.tell() != self.readptr:
            self.fd.seek(self.readptr)

        result = self.fd.read(length)
        self.readptr += len(result)
        return result

    def WriteStream(self, stream, progress=None):
        """Copy the stream into this stream."""
        while True:
            data = stream.read(BUFF_SIZE)
            if not data:
                break

            self.Write(data)
            progress.Report(self.readptr)

    def Write(self, data):
        self.MarkDirty()

        if self.fd.tell() != self.readptr:
            self.fd.seek(self.readptr)

        self.fd.write(data)
        # self.fd.flush()
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


def GenericFileHandler(resolver, urn):
    if os.path.isdir(urn.ToFilename()):
        directory_handler = registry.AFF4_TYPE_MAP[lexicon.AFF4_DIRECTORY_TYPE]
        result = directory_handler(resolver)
        resolver.Set(result.urn, lexicon.AFF4_STORED, urn)

        return result

    return FileBackedObject(resolver, urn)

registry.AFF4_TYPE_MAP["file"] = GenericFileHandler
registry.AFF4_TYPE_MAP[lexicon.AFF4_FILE_TYPE] = FileBackedObject


class AFF4MemoryStream(FileBackedObject):

    def __init__(self, *args, **kwargs):
        super(AFF4MemoryStream, self).__init__(*args, **kwargs)
        self.fd = StringIO.StringIO()
