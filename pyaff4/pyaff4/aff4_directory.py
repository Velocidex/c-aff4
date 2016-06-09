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

"""This module implements the Directory AFF4 Volume."""
import logging
import os

from pyaff4 import aff4
from pyaff4 import aff4_utils
from pyaff4 import lexicon
from pyaff4 import rdfvalue
from pyaff4 import registry

LOGGER = logging.getLogger("pyaff4")


class AFF4Directory(aff4.AFF4Volume):

    root_path = ""

    @classmethod
    def NewAFF4Directory(cls, resolver, root_urn):
        result = AFF4Directory(resolver)
        result.root_path = root_urn.ToFilename()

        mode = resolver.Get(root_urn, lexicon.AFF4_STREAM_WRITE_MODE)
        if mode == "truncate":
            aff4_utils.RemoveDirectory(result.root_path)

        if not (os.path.isdir(result.root_path) or
                os.path.isfile(result.root_path)):
            if mode == "truncate" or mode == "append":
                aff4_utils.MkDir(result.root_path)
            else:
                raise RuntimeError("Unknown mode")

        resolver.Set(result.urn, lexicon.AFF4_TYPE,
                     rdfvalue.URN(lexicon.AFF4_DIRECTORY_TYPE))
        resolver.Set(result.urn, lexicon.AFF4_STORED,
                     rdfvalue.URN(root_urn))

        result.LoadFromURN()

        return resolver.CachePut(result)

    def __init__(self, *args, **kwargs):
        super(AFF4Directory, self).__init__(*args, **kwargs)
        self.children = set()

    def CreateMember(self, child_urn):
        # Check that child is a relative path in our URN.
        relative_path = self.urn.RelativePath(child_urn)
        if relative_path == child_urn.SerializeToString():
            raise IOError("Child URN is not within container URN.")

        # Use this filename. Note that since filesystems can not typically
        # represent files and directories as the same path component we can not
        # allow slashes in the filename. Otherwise we will fail to create
        # e.g. stream/0000000 and stream/0000000/index.
        filename = aff4_utils.member_name_for_urn(
            child_urn, self.urn, slash_ok=False)

        # We are allowed to create any files inside the directory volume.
        self.resolver.Set(child_urn, lexicon.AFF4_TYPE,
                          rdfvalue.URN(lexicon.AFF4_FILE_TYPE))
        self.resolver.Set(child_urn, lexicon.AFF4_STREAM_WRITE_MODE,
                          rdfvalue.XSDString("truncate"))
        self.resolver.Set(child_urn, lexicon.AFF4_DIRECTORY_CHILD_FILENAME,
                          rdfvalue.XSDString(filename))

        # Store the member inside our storage location.
        self.resolver.Set(
            child_urn, lexicon.AFF4_FILE_NAME,
            rdfvalue.XSDString(self.root_path + os.sep + filename))

        result = self.resolver.AFF4FactoryOpen(child_urn)
        self.MarkDirty()
        self.children.add(child_urn)

        return result

    def LoadFromURN(self):
        self.storage = self.resolver.Get(self.urn, lexicon.AFF4_STORED)
        if not self.storage:
            LOGGER.error("Unable to find storage for AFF4Directory %s",
                         self.urn)
            raise IOError("NOT_FOUND")

        # The actual filename for the root directory.
        self.root_path = self.storage.ToFilename()

        try:
            # We need to get the URN of the container before we can process
            # anything.
            with self.resolver.AFF4FactoryOpen(
                    self.storage.Append(
                        lexicon.AFF4_CONTAINER_DESCRIPTION)) as desc:
                if desc:
                    urn_string = desc.Read(1000)

                    if (urn_string and
                            self.urn.SerializeToString() != urn_string):
                        self.resolver.DeleteSubject(self.urn)
                        self.urn.Set(urn_string)

                    # Set these triples with the new URN so we know how to open
                    # it.
                    self.resolver.Set(self.urn, lexicon.AFF4_TYPE,
                                      rdfvalue.URN(lexicon.AFF4_DIRECTORY_TYPE))

                    self.resolver.Set(self.urn, lexicon.AFF4_STORED,
                                      rdfvalue.URN(self.storage))

                    LOGGER.info("AFF4Directory volume found: %s", self.urn)

            # Try to load the RDF metadata file from the storage.
            with self.resolver.AFF4FactoryOpen(
                    self.storage.Append(
                        lexicon.AFF4_CONTAINER_INFO_TURTLE)) as turtle_stream:
                if turtle_stream:
                    self.resolver.LoadFromTurtle(turtle_stream)

                    # Find all the contained objects and adjust their filenames.
                    for subject in self.resolver.SelectSubjectsByPrefix(
                            self.urn):

                        child_filename = self.resolver.Get(
                            subject, lexicon.AFF4_DIRECTORY_CHILD_FILENAME)
                        if child_filename:
                            self.resolver.Set(
                                subject, lexicon.AFF4_FILE_NAME,
                                rdfvalue.XSDString(
                                    self.root_path +
                                    os.sep +
                                    child_filename.SerializeToString()))

        except IOError:
            pass


    def Flush(self):
        if self.IsDirty():
            # Flush all children before us. This ensures that metadata is fully
            # generated for each child.
            for child_urn in list(self.children):
                obj = self.resolver.CacheGet(child_urn)
                if obj:
                    obj.Flush()

            # Mark the container with its URN
            with self.CreateMember(
                    self.urn.Append(
                        lexicon.AFF4_CONTAINER_DESCRIPTION)) as desc:
                desc.Truncate()
                desc.Write(self.urn.SerializeToString())
                desc.Flush() # Flush explicitly since we already flushed above.

            # Dump the resolver into the zip file.
            with self.CreateMember(
                    self.urn.Append(
                        lexicon.AFF4_CONTAINER_INFO_TURTLE)) as turtle_stream:
                # Overwrite the old turtle file with the newer data.
                turtle_stream.Truncate()
                self.resolver.DumpToTurtle(turtle_stream, verbose=False)
                turtle_stream.Flush()

        return super(AFF4Directory, self).Flush()


registry.AFF4_TYPE_MAP[lexicon.AFF4_DIRECTORY_TYPE] = AFF4Directory
