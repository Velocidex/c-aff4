# Copyright 2016 Google Inc. All rights reserved.
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

"""This module implements the AFF4 Google Cloud Volume.

It allows use of cloud buckets as transparent AFF4 images.
"""
import json
import logging
from multiprocessing.dummy import Pool as ThreadPool
import tempfile
import time
import threading
import os


from gcloud import storage
from pyaff4 import aff4_directory
from pyaff4 import aff4_file
from pyaff4 import aff4_utils
from pyaff4 import lexicon
from pyaff4 import rdfvalue
from pyaff4 import registry


# Lexicon specific to cloud storage.
GOOGLE_NAMESPACE = "http://www.google.com#"
AFF4_GCS_TYPE = (GOOGLE_NAMESPACE + "cloud_storage_directory")
AFF4_GCS_STREAM_TYPE = (GOOGLE_NAMESPACE + "cloud_storage_stream")
AFF4_GCS_STREAM_LOCATION = (GOOGLE_NAMESPACE + "cloud_storage_url")

# Global client handle.
GCE_CLIENT = {}

def get_client():
    thread_id = threading.currentThread().ident
    cred_file = os.environ.get("GOOGLE_APPLICATION_CREDENTIALS")
    if cred_file is None:
        raise RuntimeError(
            "No service account credentials specified. "
            "See https://support.google.com/cloud/answer/6158862")

    if GCE_CLIENT.get(thread_id) is None:
        creds = json.load(open(cred_file))

        GCE_CLIENT[thread_id] = storage.Client(project=creds["project_id"])

    return GCE_CLIENT[thread_id]


LOGGER = logging.getLogger("pyaff4.cloud")
LOGGER.setLevel(logging.INFO)


class AFF4GStore(aff4_directory.AFF4Directory):
    """An AFF4 volume based on cloud storage."""

    # Our bucket name.
    bucket = ""

    @classmethod
    def NewAFF4GStore(cls, resolver, root_urn):
        urn_parts = root_urn.Parse()
        result = AFF4GStore(resolver)
        result.root_path = urn_parts.path

        # Get a client handle.
        gcs = get_client()

        result.bucket = gcs.get_bucket(urn_parts.netloc)

        result.mode = resolver.Get(root_urn, lexicon.AFF4_STREAM_WRITE_MODE)

        resolver.Set(result.urn, lexicon.AFF4_TYPE,
                     rdfvalue.URN(AFF4_GCS_TYPE))
        resolver.Set(result.urn, lexicon.AFF4_STORED,
                     rdfvalue.URN(root_urn))

        result.LoadFromURN()

        return resolver.CachePut(result)

    def CreateMember(self, child_urn):
        # Check that child is a relative path in our URN.
        relative_path = self.urn.RelativePath(child_urn)
        if relative_path == child_urn.SerializeToString():
            raise IOError("Child URN is not within container URN.")

        # The member will also be located in the cloud as a stream.
        self.resolver.Set(child_urn, lexicon.AFF4_TYPE,
                          rdfvalue.URN(AFF4_GCS_STREAM_TYPE))

        # It will be stored in this volume.
        self.resolver.Set(child_urn, lexicon.AFF4_STORED, self.urn)
        self.resolver.Set(child_urn, AFF4_GCS_STREAM_LOCATION,
                          self.storage.Append(relative_path))

        # We are allowed to create any files inside the directory volume.
        self.resolver.Set(child_urn, lexicon.AFF4_STREAM_WRITE_MODE,
                          rdfvalue.XSDString("truncate"))

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

        if self.mode == "truncate":
            return

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

        except IOError:
            pass


class AFF4GCSStream(aff4_file.FileBackedObject):
    """A stream stored in cloud storage."""

    def __init__(self, *args, **kwargs):
        super(AFF4GCSStream, self).__init__(*args, **kwargs)

        if self.resolver.flush_callbacks.get("CloudThreadPool") is None:
            self.resolver.CloudThreadPool = ThreadPool(4)
            def close(pool=self.resolver.CloudThreadPool):
                pool.close()
                pool.join()

            self.resolver.flush_callbacks["CloudThreadPool"] = close

    def _get_storage_urn(self):
        if self.urn.Parse().scheme == "gs":
            storage_urn = self.urn
        else:
            # Try to find where we are actually stored.
            storage_urn = self.resolver.Get(self.urn, AFF4_GCS_STREAM_LOCATION)
            if not storage_urn:
                raise RuntimeError("Unable to determine storage for %s",
                                   self.urn)

        return storage_urn

    def _fetch_blob_data(self):
        storage_parts = self._get_storage_urn().Parse()
        gcs = get_client()

        # The bucket that contains this stream.
        bucket = gcs.get_bucket(storage_parts.netloc)
        blob_name = storage_parts.path.strip("/")

        # Retry 3 times.
        for i in range(3):
            try:
                blob = bucket.get_blob(blob_name)
                if blob is not None:
                    LOGGER.info("Fetching %s to local cache.", self.urn)
                    return blob.download_as_string()
            except IOError:
                LOGGER.info("Retrying blob download: %s (%s)", blob_name, i)

        return ""

    def LoadFromURN(self):
        mode = self.resolver.Get(self.urn, lexicon.AFF4_STREAM_WRITE_MODE)
        if mode == "append":
            raise RuntimeError("Cloud storage does not support appending.")

        # If there is a dedicated cache directory we should use it first.
        cache_directory = self.resolver.Get(lexicon.AFF4_CONFIG_CACHE_DIR,
                                            lexicon.AFF4_FILE_NAME)

        # A persistent cache directory is set.
        if cache_directory:
            filename = aff4_utils.member_name_for_urn(
                self.urn, base_urn=rdfvalue.URN(""),
                slash_ok=False)

            filename = os.path.join(
                unicode(cache_directory), filename)

            # When truncating a stream we just overwrite it with new data.
            if mode == "truncate":
                self.fd = open(filename, "w+b")
            else:
                try:
                    self.fd = open(filename, "rb")
                    self.fd.seek(0, 2)
                    self.size = self.fd.tell()
                except IOError:
                    # Try to fetch the data so we can write it into the local
                    # cache file.
                    LOGGER.info("Creating cached file %s", filename)
                    aff4_utils.EnsureDirectoryExists(filename)
                    try:
                        blob_data = self._fetch_blob_data()
                        self.fd = open(filename, "w+b")
                        self.fd.write(blob_data)
                    except IOError as e:
                        LOGGER.error(
                            "Unable to write on cache directory %s: %s",
                            filename, e)

                        raise

                return

        else:
            # No dedicated cache directory, so we just get a temp file to work
            # from.
            blob_data = self._fetch_blob_data()
            self.fd = tempfile.NamedTemporaryFile(prefix="pyaff4")
            if mode != "truncate":
                self.fd.write(blob_data)

    def _async_flush(self):
        try:
            now = time.time()
            storage_urn = self._get_storage_urn()
            LOGGER.info("Flushing %s -> %s (length %s)", self.urn, storage_urn,
                        self.Size())
            gcs = get_client()
            urn_parts = storage_urn.Parse()

            # The bucket that contains this stream.
            bucket = gcs.get_bucket(urn_parts.netloc)
            blob_name = urn_parts.path.strip("/")

            blob = bucket.blob(blob_name)

            self.fd.seek(0)
            blob.upload_from_file(self.fd)

            LOGGER.info("Flushed %s (%s) in %s Sec",
                        self.urn, self.Size(), int(time.time() - now))
        except Exception as e:
            LOGGER.exception("Error: %s", e)

    def Flush(self):
        # Sync the internal cache with the blob store.
        if self.IsDirty():
            self.resolver.CloudThreadPool.apply_async(self._async_flush)

        else:
            self.fd.close()

        super(AFF4GCSStream, self).Flush()


def GenericGCSHandler(resolver, urn):
    # This is a bucket urn.
    if not urn.Parse().path.strip("/"):
        directory_handler = registry.AFF4_TYPE_MAP[lexicon.AFF4_DIRECTORY_TYPE]
        result = directory_handler(resolver)
        resolver.Set(result.urn, lexicon.AFF4_STORED, urn)

        return result

    return AFF4GCSStream(resolver, urn)

registry.AFF4_TYPE_MAP["gs"] = GenericGCSHandler
registry.AFF4_TYPE_MAP[AFF4_GCS_TYPE] = AFF4GStore
registry.AFF4_TYPE_MAP[AFF4_GCS_STREAM_TYPE] = AFF4GCSStream
