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

"""This module implements the standard AFF4 Image."""
import logging
import struct
import zlib

try:
    import snappy
except ImportError:
    snappy = None

from pyaff4 import aff4
from pyaff4 import lexicon
from pyaff4 import rdfvalue
from pyaff4 import registry


LOGGER = logging.getLogger("pyaff4")


class _CompressorStream(object):
    """A stream which chunks up another stream.

    Each read() operation will return a compressed chunk.
    """
    def __init__(self, owner, stream):
        self.owner = owner
        self.stream = stream
        self.chunk_count_in_bevy = 0
        self.size = 0
        self.bevy_index = []
        self.bevy_length = 0

    def tell(self):
        return self.stream.tell()

    def read(self, _):
        # Stop copying when the bevy is full.
        if self.chunk_count_in_bevy >= self.owner.chunks_per_segment:
            return ""

        chunk = self.stream.read(self.owner.chunk_size)
        if not chunk:
            return ""

        self.size += len(chunk)

        if self.owner.compression == lexicon.AFF4_IMAGE_COMPRESSION_ZLIB:
            compressed_chunk = zlib.compress(chunk)
        elif (snappy and self.owner.compression ==
              lexicon.AFF4_IMAGE_COMPRESSION_SNAPPY):
            compressed_chunk = snappy.compress(chunk)
        elif self.owner.compression == lexicon.AFF4_IMAGE_COMPRESSION_STORED:
            compressed_chunk = chunk

        self.bevy_index.append(self.bevy_length)
        self.bevy_length += len(compressed_chunk)
        self.chunk_count_in_bevy += 1

        return compressed_chunk


class AFF4Image(aff4.AFF4Stream):

    @staticmethod
    def NewAFF4Image(resolver, image_urn, volume_urn):
        with resolver.AFF4FactoryOpen(volume_urn) as volume:
            # Inform the volume that we have a new image stream contained within
            # it.
            volume.children.add(image_urn)

            resolver.Set(image_urn, lexicon.AFF4_TYPE, rdfvalue.URN(
                lexicon.AFF4_IMAGE_TYPE))

            resolver.Set(image_urn, lexicon.AFF4_STORED,
                         rdfvalue.URN(volume_urn))

            return resolver.AFF4FactoryOpen(image_urn)

    def LoadFromURN(self):
        volume_urn = self.resolver.Get(self.urn, lexicon.AFF4_STORED)
        if not volume_urn:
            raise IOError("Unable to find storage for urn %s" % self.urn)

        self.chunk_size = int(self.resolver.Get(
            self.urn, lexicon.AFF4_IMAGE_CHUNK_SIZE) or 32*1024)

        self.chunks_per_segment = int(self.resolver.Get(
            self.urn, lexicon.AFF4_IMAGE_CHUNKS_PER_SEGMENT) or 1024)

        self.size = int(
            self.resolver.Get(self.urn, lexicon.AFF4_STREAM_SIZE) or 0)

        self.compression = str(self.resolver.Get(
            self.urn, lexicon.AFF4_IMAGE_COMPRESSION) or
                               lexicon.AFF4_IMAGE_COMPRESSION_ZLIB)

        # A buffer for overlapped writes which do not fit into a chunk.
        self.buffer = ""

        # Compressed chunks in the bevy.
        self.bevy = []

        # Length of all chunks in the bevy.
        self.bevy_length = 0

        # List of bevy offsets.
        self.bevy_index = []
        self.chunk_count_in_bevy = 0
        self.bevy_number = 0

    def WriteStream(self, source_stream, progress=None):
        """Copy data from a source stream into this stream."""
        if progress is None:
            progress = aff4.DEFAULT_PROGRESS

        volume_urn = self.resolver.Get(self.urn, lexicon.AFF4_STORED)
        if not volume_urn:
            raise IOError("Unable to find storage for urn %s" %
                          self.urn)

        with self.resolver.AFF4FactoryOpen(volume_urn) as volume:
            # Write a bevy at a time.
            while 1:
                stream = _CompressorStream(self, source_stream)

                bevy_urn = self.urn.Append("%08d" % self.bevy_number)
                bevy_index_urn = bevy_urn.Append("index")

                progress.start = (self.bevy_number *
                                  self.chunks_per_segment *
                                  self.chunk_size)
                with volume.CreateMember(bevy_urn) as bevy:
                    bevy.WriteStream(stream, progress=progress)

                with volume.CreateMember(bevy_index_urn) as bevy_index:
                    bevy_index.Write(
                        struct.pack("<" + "L" * len(stream.bevy_index),
                                    *stream.bevy_index))

                # Make another bevy.
                self.bevy_number += 1
                self.size += stream.size
                self.readptr += stream.size

                # Last iteration - the compressor stream quit before the bevy is
                # full.
                if stream.chunk_count_in_bevy != self.chunks_per_segment:
                    break

        self._write_metadata()

    def Write(self, data):
        self.MarkDirty()
        self.buffer += data
        idx = 0

        while len(self.buffer) - idx > self.chunk_size:
            chunk = self.buffer[idx:idx+self.chunk_size]
            idx += self.chunk_size
            self.FlushChunk(chunk)

        self.buffer = self.buffer[idx:]

        self.readptr += len(data)
        if self.readptr > self.size:
            self.size = self.readptr

        return len(data)

    def FlushChunk(self, chunk):
        bevy_offset = self.bevy_length

        if self.compression == lexicon.AFF4_IMAGE_COMPRESSION_ZLIB:
            compressed_chunk = zlib.compress(chunk)
        elif (snappy and self.compression ==
              lexicon.AFF4_IMAGE_COMPRESSION_SNAPPY):
            compressed_chunk = snappy.compress(chunk)
        elif self.compression == lexicon.AFF4_IMAGE_COMPRESSION_STORED:
            compressed_chunk = chunk

        self.bevy_index.append(bevy_offset)
        self.bevy.append(compressed_chunk)
        self.bevy_length += len(compressed_chunk)
        self.chunk_count_in_bevy += 1

        if self.chunk_count_in_bevy >= self.chunks_per_segment:
            self._FlushBevy()

    def _FlushBevy(self):
        volume_urn = self.resolver.Get(self.urn, lexicon.AFF4_STORED)
        if not volume_urn:
            raise IOError("Unable to find storage for urn %s" % self.urn)

        # Bevy is empty nothing to do.
        if not self.bevy:
            return

        bevy_urn = self.urn.Append("%08d" % self.bevy_number)
        bevy_index_urn = bevy_urn.Append("index")

        with self.resolver.AFF4FactoryOpen(volume_urn) as volume:
            with volume.CreateMember(bevy_index_urn) as bevy_index:
                bevy_index.Write(
                    struct.pack("<" + "L" * len(self.bevy_index),
                                *self.bevy_index))

            with volume.CreateMember(bevy_urn) as bevy:
                bevy.Write("".join(self.bevy))

            # We dont need to hold these in memory any more.
            self.resolver.Close(bevy_index)
            self.resolver.Close(bevy)

        # In Python it is more efficient to keep a list of chunks and then join
        # them at the end in one operation.
        self.chunk_count_in_bevy = 0
        self.bevy_number += 1
        self.bevy = []
        self.bevy_index = []
        self.bevy_length = 0

    def _write_metadata(self):
        self.resolver.Set(self.urn, lexicon.AFF4_TYPE,
                          rdfvalue.URN(lexicon.AFF4_IMAGE_TYPE))

        self.resolver.Set(self.urn, lexicon.AFF4_IMAGE_CHUNK_SIZE,
                          rdfvalue.XSDInteger(self.chunk_size))

        self.resolver.Set(self.urn, lexicon.AFF4_IMAGE_CHUNKS_PER_SEGMENT,
                          rdfvalue.XSDInteger(self.chunks_per_segment))

        self.resolver.Set(self.urn, lexicon.AFF4_STREAM_SIZE,
                          rdfvalue.XSDInteger(self.Size()))

        self.resolver.Set(
            self.urn, lexicon.AFF4_IMAGE_COMPRESSION,
            rdfvalue.URN(self.compression))

    def Flush(self):
        if self.IsDirty():
            # Flush the last chunk.
            self.FlushChunk(self.buffer)
            self._FlushBevy()

            self._write_metadata()

        return super(AFF4Image, self).Flush()

    def Read(self, length):
        if length == 0:
            return ""

        length = min(length, self.Size() - self.readptr)

        initial_chunk_id, initial_chunk_offset = divmod(self.readptr,
                                                        self.chunk_size)

        final_chunk_id, _ = divmod(self.readptr + length - 1, self.chunk_size)

        # We read this many full chunks at once.
        chunks_to_read = final_chunk_id - initial_chunk_id + 1
        chunk_id = initial_chunk_id
        result = ""

        while chunks_to_read > 0:
            chunks_read, data = self._ReadPartial(chunk_id, chunks_to_read)
            if chunks_read == 0:
                break

            chunks_to_read -= chunks_read
            result += data

        if initial_chunk_offset:
            result = result[initial_chunk_offset:]

        result = result[:length]

        self.readptr += len(result)

        return result

    def _ReadPartial(self, chunk_id, chunks_to_read):
        chunks_read = 0
        result = ""

        while chunks_to_read > 0:
            bevy_id = chunk_id / self.chunks_per_segment
            bevy_urn = self.urn.Append("%08d" % bevy_id)
            bevy_index_urn = bevy_urn.Append("index")

            with self.resolver.AFF4FactoryOpen(bevy_index_urn) as bevy_index:
                index_size = bevy_index.Size() / 4
                bevy_index_data = bevy_index.Read(bevy_index.Size())

                bevy_index_array = struct.unpack(
                    "<" + "I" * index_size, bevy_index_data)

            with self.resolver.AFF4FactoryOpen(bevy_urn) as bevy:
                while chunks_to_read > 0:
                    # Read a full chunk from the bevy.
                    data = self._ReadChunkFromBevy(
                        chunk_id, bevy, bevy_index_array, index_size)

                    result += data

                    chunks_to_read -= 1
                    chunk_id += 1
                    chunks_read += 1

                    # This bevy is exhausted, get the next one.
                    if bevy_id < chunk_id / self.chunks_per_segment:
                        break

        return chunks_read, result

    def _ReadChunkFromBevy(self, chunk_id, bevy, bevy_index, index_size):
        chunk_id_in_bevy = chunk_id % self.chunks_per_segment

        if index_size == 0:
            LOGGER.error("Index empty in %s: %s", self.urn, chunk_id)
            raise IOError("Index empty in %s: %s" % (self.urn, chunk_id))
        # The segment is not completely full.
        if chunk_id_in_bevy >= index_size:
            LOGGER.error("Bevy index too short in %s: %s",
                         self.urn, chunk_id)
            raise IOError("Bevy index too short in %s: %s" % (
                self.urn, chunk_id))

        # For the last chunk in the bevy, consume to the end of the bevy
        # segment.
        if chunk_id_in_bevy == index_size - 1:
            compressed_chunk_size = bevy.Size() - bevy.Tell()
        else:
            compressed_chunk_size = (bevy_index[chunk_id_in_bevy + 1] -
                                     bevy_index[chunk_id_in_bevy])

        bevy.Seek(bevy_index[chunk_id_in_bevy], 0)
        cbuffer = bevy.Read(compressed_chunk_size)
        if self.compression == lexicon.AFF4_IMAGE_COMPRESSION_ZLIB:
            return zlib.decompress(cbuffer)

        if snappy and self.compression == lexicon.AFF4_IMAGE_COMPRESSION_SNAPPY:
            return snappy.decompress(cbuffer)

        if self.compression == lexicon.AFF4_IMAGE_COMPRESSION_STORED:
            return cbuffer

        raise RuntimeError(
            "Unable to process compression %s" % self.compression)


registry.AFF4_TYPE_MAP[lexicon.AFF4_IMAGE_TYPE] = AFF4Image
