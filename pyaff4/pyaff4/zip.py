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
import StringIO
import zlib

from pyaff4 import aff4
from pyaff4 import aff4_file
from pyaff4 import aff4_utils
from pyaff4 import lexicon
from pyaff4 import rdfvalue
from pyaff4 import registry
from pyaff4 import struct_parser

LOGGER = logging.getLogger("pyaff4")

# Compression modes we support inside Zip files (Note this is not the same as
# the aff4_image compression.
ZIP_STORED = 0
ZIP_DEFLATE = 8


BUFF_SIZE = 64 * 1024


class EndCentralDirectory(struct_parser.CreateStruct(
        "EndCentralDirectory_t",
        definition="""
        uint32_t magic = 0x6054b50;
        uint16_t number_of_this_disk = 0;
        uint16_t disk_with_cd = 0;
        uint16_t total_entries_in_cd_on_disk;
        uint16_t total_entries_in_cd;
        int32_t size_of_cd = -1;
        int32_t offset_of_cd = -1;
        uint16_t comment_len = 0;
        """)):

    magic_string = 'PK\x05\x06'

    def IsValid(self):
        return self.magic == 0x6054b50

    @classmethod
    def FromBuffer(cls, buffer):
        """Instantiate an EndCentralDirectory from this buffer."""
        # Not enough data to contain an EndCentralDirectory
        if len(buffer) > cls.sizeof():
            # Scan the buffer backwards for an End of Central Directory magic
            end = len(buffer) - cls.sizeof() + 4
            while True:
                index = buffer.rfind(cls.magic_string, 0, end)
                if index < 0:
                    break

                end_cd = cls(buffer[index:])
                if end_cd.IsValid():
                    return end_cd, index

                end = index

        raise IOError("Unable to find EndCentralDirectory")


class CDFileHeader(struct_parser.CreateStruct(
        "CDFileHeader_t",
        """
        uint32_t magic = 0x2014b50;
        uint16_t version_made_by = 0x317;
        uint16_t version_needed = 0x14;
        uint16_t flags = 0x8;
        uint16_t compression_method;
        uint16_t dostime;
        uint16_t dosdate;
        int32_t crc32;
        int32_t compress_size = -1;
        int32_t file_size = -1;
        uint16_t file_name_length;
        uint16_t extra_field_len = 32;
        uint16_t file_comment_length = 0;
        uint16_t disk_number_start = 0;
        uint16_t internal_file_attr = 0;
        uint32_t external_file_attr = 0644 << 16L;
        int32_t relative_offset_local_header = -1;
        """)):
    def IsValid(self):
        return self.magic == 0x2014b50


class ZipFileHeader(struct_parser.CreateStruct(
        "ZipFileHeader_t",
        """
        uint32_t magic = 0x4034b50;
        uint16_t version = 0x14;
        uint16_t flags = 0x8;
        uint16_t compression_method;
        uint16_t lastmodtime;
        uint16_t lastmoddate;
        int32_t crc32;
        int32_t compress_size;
        int32_t file_size;
        uint16_t file_name_length;
        uint16_t extra_field_len;
        """)):

    def IsValid(self):
        return self.magic == 0x4034b50


class Zip64FileHeaderExtensibleField(struct_parser.CreateStruct(
        "Zip64FileHeaderExtensibleField_t",
        """
        uint16_t header_id = 1;
        uint16_t data_size = 28;
        uint64_t file_size;
        uint64_t compress_size;
        uint64_t relative_offset_local_header;
        uint32_t disk_number_start = 0;
        """)):
    pass

class Zip64EndCD(struct_parser.CreateStruct(
        "Zip64EndCD_t",
        """
        uint32_t magic = 0x06064b50;
        uint64_t size_of_header = 0;
        uint16_t version_made_by = 0x2d;
        uint16_t version_needed = 0x2d;
        uint32_t number_of_disk = 0;
        uint32_t number_of_disk_with_cd = 0;
        uint64_t number_of_entries_in_volume;
        uint64_t number_of_entries_in_total;
        uint64_t size_of_cd;
        uint64_t offset_of_cd;
        """)):

    def IsValid(self):
        return self.magic == 0x06064b50


class Zip64CDLocator(struct_parser.CreateStruct(
        "Zip64CDLocator_t",
        """
        uint32_t magic = 0x07064b50;
        uint32_t disk_with_cd = 0;
        uint64_t offset_of_end_cd;
        uint32_t number_of_disks = 1;
        """)):

    def IsValid(self):
        return (self.magic == 0x07064b50 and
                self.disk_with_cd == 0 and
                self.number_of_disks == 1)


class ZipInfo(object):
    def __init__(self, compression_method=0, compress_size=0,
                 file_size=0, filename="", local_header_offset=0,
                 crc32=0, lastmoddate=0, lastmodtime=0):
        self.compression_method = compression_method
        self.compress_size = compress_size
        self.file_size = file_size
        self.filename = filename
        self.local_header_offset = local_header_offset
        self.crc32 = crc32
        self.lastmoddate = lastmoddate
        self.lastmodtime = lastmodtime

        self.file_header_offset = None

    def WriteFileHeader(self, backing_store):
        if self.file_header_offset is None:
            self.file_header_offset = backing_store.Tell()

        header = ZipFileHeader(
            crc32=self.crc32,
            compress_size=-1,
            file_size=-1,
            file_name_length=len(self.filename),
            compression_method=self.compression_method,
            lastmodtime=self.lastmodtime,
            lastmoddate=self.lastmoddate,
            extra_field_len=Zip64FileHeaderExtensibleField.sizeof())

        backing_store.Seek(self.file_header_offset)
        backing_store.Write(header.Pack())
        backing_store.write(self.filename)

        header_64 = Zip64FileHeaderExtensibleField(
            file_size=self.file_size,
            compress_size=self.compress_size,
            relative_offset_local_header=self.local_header_offset)

        backing_store.Write(header_64.Pack())

    def WriteCDFileHeader(self, backing_store):
        header = CDFileHeader(
            compression_method=self.compression_method,
            crc32=self.crc32,
            file_name_length=len(self.filename),
            dostime=self.lastmodtime,
            dosdate=self.lastmoddate,
            extra_field_len=Zip64FileHeaderExtensibleField.sizeof())

        backing_store.write(header.Pack())
        backing_store.write(self.filename)

        zip64header = Zip64FileHeaderExtensibleField(
            file_size=self.file_size,
            compress_size=self.compress_size,
            relative_offset_local_header=self.local_header_offset)

        backing_store.write(zip64header.Pack())


class FileWrapper(object):
    """Maps a slice from a file URN."""

    def __init__(self, resolver, file_urn, slice_offset, slice_size):
        self.file_urn = file_urn
        self.resolver = resolver
        self.slice_size = slice_size
        self.slice_offset = slice_offset
        self.readptr = 0

    def seek(self, offset, whence=0):
        if whence == 0:
            self.readptr = offset
        elif whence == 1:
            self.readptr += offset
        elif whence == 2:
            self.readptr = self.slice_size + offset

    def tell(self):
        return self.readptr

    def read(self, length):
        with self.resolver.AFF4FactoryOpen(self.file_urn) as fd:
            fd.seek(self.slice_offset + self.readptr)
            to_read = min(self.slice_size - self.readptr, length)
            result = fd.read(to_read)
            self.readptr += len(result)

            return result


def DecompressBuffer(buffer):
    """Decompress using deflate a single buffer.

    We assume the buffer is not too large.
    """
    decompressor = zlib.decompressobj(-15)
    result = decompressor.decompress(buffer, len(buffer))

    return result + decompressor.flush()


class ZipFileSegment(aff4_file.FileBackedObject):
    compression_method = ZIP_STORED

    def LoadFromURN(self):
        owner_urn = self.resolver.Get(self.urn, lexicon.AFF4_STORED)
        with self.resolver.AFF4FactoryOpen(owner_urn) as owner:
            self.LoadFromZipFile(owner)

    def LoadFromZipFile(self, owner):
        """Read the segment data from the ZipFile owner."""
        member_name = aff4_utils.member_name_for_urn(self.urn, owner.urn)

        # Parse the ZipFileHeader for this filename.
        zip_info = owner.members.get(member_name)
        if zip_info is None:
            # The owner does not have this file yet - we add it when closing.
            self.fd = StringIO.StringIO()
            return

        backing_store_urn = owner.backing_store_urn
        with self.resolver.AFF4FactoryOpen(backing_store_urn) as backing_store:
            backing_store.Seek(
                zip_info.local_header_offset + owner.global_offset, 0)
            file_header = ZipFileHeader(
                backing_store.Read(ZipFileHeader.sizeof()))

            if not file_header.IsValid():
                raise IOError("Local file header invalid!")

            # The filename should be null terminated.
            file_header_filename = backing_store.Read(
                file_header.file_name_length).split("\x00")[0]

            if file_header_filename != zip_info.filename:
                msg = (u"Local filename %s different from "
                       u"central directory %s.") % (
                           file_header_filename, zip_info.filename)
                LOGGER.error(msg)
                raise IOError(msg)

            backing_store.Seek(file_header.extra_field_len, aff4.SEEK_CUR)

            buffer_size = zip_info.file_size
            if file_header.compression_method == ZIP_DEFLATE:
                # We write the entire file in a memory buffer if we need to
                # deflate it.
                self.compression_method = ZIP_DEFLATE
                c_buffer = backing_store.Read(zip_info.compress_size)
                decomp_buffer = DecompressBuffer(c_buffer)
                if len(decomp_buffer) != buffer_size:
                    LOGGER.info("Unable to decompress file %s", self.urn)
                    raise IOError()

                self.fd = StringIO.StringIO(decomp_buffer)

            elif file_header.compression_method == ZIP_STORED:
                # Otherwise we map a slice into it.
                self.fd = FileWrapper(self.resolver, backing_store_urn,
                                      backing_store.Tell(), buffer_size)

            else:
                LOGGER.info("Unsupported compression method.")
                raise NotImplementedError()

    def WriteStream(self, stream, progress=None):
        owner_urn = self.resolver.Get(self.urn, lexicon.AFF4_STORED)
        with self.resolver.AFF4FactoryOpen(owner_urn) as owner:
            owner.StreamAddMember(
                self.urn, stream, compression_method=self.compression_method,
                progress=progress)

    def Flush(self):
        if self.IsDirty():
            owner_urn = self.resolver.Get(self.urn, lexicon.AFF4_STORED)
            with self.resolver.AFF4FactoryOpen(owner_urn) as owner:
                self.Seek(0)

                # Copy ourselves into the owner.
                owner.StreamAddMember(
                    self.urn, self, self.compression_method)

        super(ZipFileSegment, self).Flush()


class ZipFile(aff4.AFF4Volume):
    def __init__(self, *args, **kwargs):
        super(ZipFile, self).__init__(*args, **kwargs)
        self.children = set()
        self.members = {}
        self.global_offset = 0

    def parse_cd(self, backing_store_urn):
        with self.resolver.AFF4FactoryOpen(backing_store_urn) as backing_store:
            # Find the End of Central Directory Record - We read about 4k of
            # data and scan for the header from the end, just in case there is
            # an archive comment appended to the end.
            backing_store.Seek(-BUFF_SIZE, 2)

            ecd_real_offset = backing_store.Tell()
            buffer = backing_store.Read(BUFF_SIZE)

            end_cd, buffer_offset = EndCentralDirectory.FromBuffer(buffer)
            ecd_real_offset += buffer_offset

            LOGGER.info("Found ECD at %#x", ecd_real_offset)

            urn_string = None

            # Fetch the volume comment.
            if end_cd.comment_len > 0:
                backing_store.Seek(ecd_real_offset + end_cd.sizeof())
                urn_string = backing_store.Read(end_cd.comment_len)

                LOGGER.info("Loaded AFF4 volume URN %s from zip file.",
                            urn_string)

            # There is a catch 22 here - before we parse the ZipFile we dont
            # know the Volume's URN, but we need to know the URN so the
            # AFF4FactoryOpen() can open it. Therefore we start with a random
            # URN and then create a new ZipFile volume. After parsing the
            # central directory we discover our URN and therefore we can delete
            # the old, randomly selected URN.
            if urn_string and self.urn != urn_string:
                self.resolver.DeleteSubject(self.urn)
                self.urn.Set(urn_string)

                # Set these triples so we know how to open the zip file again.
                self.resolver.Set(self.urn, lexicon.AFF4_TYPE, rdfvalue.URN(
                    lexicon.AFF4_ZIP_TYPE))
                self.resolver.Set(self.urn, lexicon.AFF4_STORED, rdfvalue.URN(
                    backing_store_urn))
                self.resolver.Set(backing_store_urn, lexicon.AFF4_CONTAINS,
                                  self.urn)

            directory_offset = end_cd.offset_of_cd
            directory_number_of_entries = end_cd.total_entries_in_cd

            # Traditional zip file - non 64 bit.
            if directory_offset > 0:
                # The global difference between the zip file offsets and real
                # file offsets. This is non zero when the zip file was appended
                # to another file.
                self.global_offset = (
                    # Real ECD offset.
                    ecd_real_offset - end_cd.size_of_cd -

                    # Claimed CD offset.
                    directory_offset)

                LOGGER.info("Global offset: %#x", self.global_offset)

            # This is a 64 bit archive, find the Zip64EndCD.
            else:
                locator_real_offset = ecd_real_offset - Zip64CDLocator.sizeof()
                backing_store.Seek(locator_real_offset, 0)
                locator = Zip64CDLocator(
                    backing_store.Read(Zip64CDLocator.sizeof()))

                if not locator.IsValid():
                    raise IOError("Zip64CDLocator invalid or not supported.")

                # Although it may appear that we can use the Zip64CDLocator to
                # locate the Zip64EndCD record via it's offset_of_cd record this
                # is not quite so. If the zip file was appended to another file,
                # the offset_of_cd field will not be valid, as it still points
                # to the old offset. In this case we also need to know the
                # global shift.
                backing_store.Seek(
                    locator_real_offset - Zip64EndCD.sizeof(), 0)

                end_cd = Zip64EndCD(
                    backing_store.Read(Zip64EndCD.sizeof()))

                if not end_cd.IsValid():
                    LOGGER.error("Zip64EndCD magic not correct @%#x",
                                 locator_real_offset - Zip64EndCD.sizeof())
                    raise RuntimeError("Zip64EndCD magic not correct")

                directory_offset = end_cd.offset_of_cd
                directory_number_of_entries = end_cd.number_of_entries_in_volume

                # The global offset is now known:
                self.global_offset = (
                    # Real offset of the central directory.
                    locator_real_offset - Zip64EndCD.sizeof() -
                    end_cd.size_of_cd -

                    # The directory offset in zip file offsets.
                    directory_offset)

                LOGGER.info("Global offset: %#x", self.global_offset)

            # Now iterate over the directory and read all the ZipInfo structs.
            entry_offset = directory_offset
            for _ in xrange(directory_number_of_entries):
                backing_store.Seek(entry_offset + self.global_offset, 0)
                entry = CDFileHeader(
                    backing_store.Read(CDFileHeader.sizeof()))

                if not entry.IsValid():
                    LOGGER.info(
                        "CDFileHeader at offset %#x invalid", entry_offset)
                    raise RuntimeError()

                zip_info = ZipInfo(
                    filename=backing_store.Read(entry.file_name_length),
                    local_header_offset=entry.relative_offset_local_header,
                    compression_method=entry.compression_method,
                    compress_size=entry.compress_size,
                    file_size=entry.file_size,
                    crc32=entry.crc32,
                    lastmoddate=entry.dosdate,
                    lastmodtime=entry.dostime)

                # Zip64 local header - parse the extra field.
                if zip_info.local_header_offset < 0:
                    # Parse all the extra field records.
                    real_end_of_extra = (
                        backing_store.Tell() + entry.extra_field_len)

                    while backing_store.Tell() < real_end_of_extra:
                        extra = Zip64FileHeaderExtensibleField(
                            backing_store.Read(entry.extra_field_len))

                        if extra.header_id == 1:
                            zip_info.local_header_offset = (
                                extra.relative_offset_local_header)
                            zip_info.file_size = extra.file_size
                            zip_info.compress_size = extra.compress_size
                            break

                if zip_info.local_header_offset >= 0:
                    LOGGER.info("Found file %s @ %#x", zip_info.filename,
                                zip_info.local_header_offset)

                    # Store this information in the resolver. Ths allows
                    # segments to be directly opened by URN.
                    member_urn = aff4_utils.urn_from_member_name(
                        zip_info.filename, self.urn)

                    self.resolver.Set(
                        member_urn, lexicon.AFF4_TYPE, rdfvalue.URN(
                            lexicon.AFF4_ZIP_SEGMENT_TYPE))

                    self.resolver.Set(member_urn, lexicon.AFF4_STORED, self.urn)
                    self.resolver.Set(member_urn, lexicon.AFF4_STREAM_SIZE,
                                      rdfvalue.XSDInteger(zip_info.file_size))
                    self.members[zip_info.filename] = zip_info

                # Go to the next entry.
                entry_offset += (entry.sizeof() +
                                 entry.file_name_length +
                                 entry.extra_field_len +
                                 entry.file_comment_length)

    @staticmethod
    def NewZipFile(resolver, backing_store_urn):
        result = ZipFile(resolver, urn=None)

        resolver.Set(result.urn, lexicon.AFF4_TYPE,
                     rdfvalue.URN(lexicon.AFF4_ZIP_TYPE))

        resolver.Set(result.urn, lexicon.AFF4_STORED,
                     rdfvalue.URN(backing_store_urn))

        return resolver.AFF4FactoryOpen(result.urn)

    def CreateMember(self, child_urn):
        member_filename = aff4_utils.member_name_for_urn(child_urn, self.urn)
        return self.CreateZipSegment(member_filename)

    def CreateZipSegment(self, filename):
        self.MarkDirty()

        segment_urn = aff4_utils.urn_from_member_name(filename, self.urn)

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
        if filename not in self.members:
            raise IOError("Segment %s does not exist yet" % filename)

        # Is it already in the cache?
        segment_urn = aff4_utils.urn_from_member_name(filename, self.urn)
        res = self.resolver.CacheGet(segment_urn)

        if res:
            LOGGER.info("Openning ZipFileSegment (cached) %s", res.urn)
            return res

        result = ZipFileSegment(resolver=self.resolver, urn=segment_urn)
        result.LoadFromZipFile(owner=self)

        LOGGER.info("Openning ZipFileSegment %s", result.urn)

        return self.resolver.CachePut(result)

    def LoadFromURN(self):
        self.backing_store_urn = self.resolver.Get(
            self.urn, lexicon.AFF4_STORED)

        if not self.backing_store_urn:
            raise IOError("Unable to load backing urn.")

        try:
            self.parse_cd(self.backing_store_urn)
        except IOError:
            # If we can not parse a CD from the zip file, this is fine, we just
            # append an AFF4 volume to it, or make a new file.
            return

        # Load the turtle metadata.
        with self.OpenZipSegment("information.turtle") as fd:
            self.resolver.LoadFromTurtle(fd)

    def StreamAddMember(self, member_urn, stream,
                        compression_method=ZIP_STORED,
                        progress=None):
        """An efficient interface to add a new archive member.

        Args:
          member_urn: The new member URN to be added.
          stream: A file-like object (with read() method) that generates data to
            be written as the member.
          compression_method: How to compress the member.

        """
        if progress is None:
            progress = aff4.EMPTY_PROGRESS

        backing_store_urn = self.resolver.Get(self.urn, lexicon.AFF4_STORED)
        with self.resolver.AFF4FactoryOpen(backing_store_urn) as backing_store:
            LOGGER.info("Writing member %s", member_urn)

            # Append member at the end of the file.
            backing_store.Seek(0, aff4.SEEK_END)

            # zip_info offsets are relative to the start of the zip file (take
            # global_offset into account).
            zip_info = ZipInfo(
                local_header_offset=backing_store.Tell() - self.global_offset,
                filename=aff4_utils.member_name_for_urn(member_urn, self.urn),
                file_size=0, crc32=0, compression_method=compression_method)

            # For now we do not support streamed writing so we need to seek back
            # to this position later with an updated crc32.
            zip_info.WriteFileHeader(backing_store)

            if compression_method == ZIP_DEFLATE:
                zip_info.compression_method = ZIP_DEFLATE
                compressor = zlib.compressobj(zlib.Z_DEFAULT_COMPRESSION,
                                              zlib.DEFLATED, -15)
                while True:
                    data = stream.read(BUFF_SIZE)
                    if not data:
                        break

                    c_data = compressor.compress(data)
                    zip_info.compress_size += len(c_data)
                    zip_info.file_size += len(data)
                    zip_info.crc32 = zlib.crc32(data, zip_info.crc32)
                    backing_store.Write(c_data)
                    progress.Report(zip_info.file_size)

                # Finalize the compressor.
                c_data = compressor.flush()
                zip_info.compress_size += len(c_data)
                backing_store.Write(c_data)

            # Just write the data directly.
            elif compression_method == ZIP_STORED:
                zip_info.compression_method = ZIP_STORED
                while True:
                    data = stream.read(BUFF_SIZE)
                    if not data:
                        break

                    zip_info.compress_size += len(data)
                    zip_info.file_size += len(data)
                    zip_info.crc32 = zlib.crc32(data, zip_info.crc32)
                    progress.Report(zip_info.file_size)
                    backing_store.Write(data)
            else:
                raise RuntimeError("Unsupported compression method")

            # Update the local file header now that CRC32 is calculated.
            zip_info.WriteFileHeader(backing_store)
            self.members[member_urn] = zip_info

    def Flush(self):
        # If the zip file was changed, re-write the central directory.
        if self.IsDirty():
            # First Flush all our children, but only if they are still in the
            # cache.
            while len(self.children):
                for child in list(self.children):
                    with self.resolver.CacheGet(child) as obj:
                        obj.Flush()

                    self.children.remove(child)

            # Add the turtle file to the volume.
            with self.CreateZipSegment("information.turtle") as turtle_segment:
                turtle_segment.compression_method = ZIP_DEFLATE

                self.resolver.DumpToTurtle(stream=turtle_segment)
                turtle_segment.Flush()

            # Write the central directory.
            self.write_zip64_CD()

        super(ZipFile, self).Flush()

    def write_zip64_CD(self):
        backing_store_urn = self.resolver.Get(self.urn, lexicon.AFF4_STORED)
        with self.resolver.AFF4FactoryOpen(backing_store_urn) as backing_store:
            # We write to a memory stream first, and then copy it into the
            # backing_store at once. This really helps when we have lots of
            # members in the zip archive.
            cd_stream = StringIO.StringIO()

            # Append a new central directory to the end of the zip file.
            backing_store.Seek(0, aff4.SEEK_END)

            # The real start of the ECD.
            ecd_real_offset = backing_store.Tell()

            total_entries = len(self.members)
            for urn, zip_info in self.members.iteritems():
                LOGGER.info("Writing CD entry for %s", urn)
                zip_info.WriteCDFileHeader(cd_stream)

            locator = Zip64CDLocator(
                offset_of_end_cd=(cd_stream.tell() + ecd_real_offset -
                                  self.global_offset))

            size_of_cd = cd_stream.tell()

            end_cd = Zip64EndCD(
                size_of_header=Zip64EndCD.sizeof()-12,
                number_of_entries_in_volume=total_entries,
                number_of_entries_in_total=total_entries,
                size_of_cd=size_of_cd,
                offset_of_cd=locator.offset_of_end_cd - size_of_cd)

            urn_string = self.urn.SerializeToString()
            end = EndCentralDirectory(
                total_entries_in_cd_on_disk=len(self.members),
                total_entries_in_cd=len(self.members),
                comment_len=len(urn_string))

            LOGGER.info("Writing Zip64EndCD at %#x",
                        cd_stream.tell() + ecd_real_offset)

            cd_stream.write(end_cd.Pack())
            cd_stream.write(locator.Pack())

            LOGGER.info("Writing ECD at %#x",
                        cd_stream.tell() + ecd_real_offset)

            cd_stream.write(end.Pack())
            cd_stream.write(urn_string)

            # Now copy the cd_stream into the backing_store in one write
            # operation.
            backing_store.write(cd_stream.getvalue())


registry.AFF4_TYPE_MAP[lexicon.AFF4_ZIP_TYPE] = ZipFile
registry.AFF4_TYPE_MAP[lexicon.AFF4_ZIP_SEGMENT_TYPE] = ZipFileSegment
