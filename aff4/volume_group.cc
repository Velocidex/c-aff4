#include "aff4/volume_group.h"
#include "aff4/aff4_image.h"
#include "aff4/aff4_map.h"
#include "aff4/zip.h"
#include "aff4/aff4_symstream.h"

namespace aff4 {




void VolumeGroup::AddVolume(AFF4Flusher<AFF4Volume> &&volume) {
    volume_objs.insert(std::make_pair(volume->urn, std::move(volume)));
}

// Construct the appropriate stream and return it.
AFF4Status VolumeGroup::GetStream(URN stream_urn, AFF4Flusher<AFF4Stream> &result) {
    // Get all the type attrbutes of the URN.
    std::vector<std::shared_ptr<RDFValue>> types;
    if (STATUS_OK == resolver->Get(stream_urn, AFF4_TYPE, types)) {
        for (auto &type : types) {
            std::string type_str(type->SerializeToString());

            if (type_str == AFF4_IMAGESTREAM_TYPE ||
                type_str == AFF4_LEGACY_IMAGESTREAM_TYPE) {
                AFF4Flusher<AFF4Image> image_stream;
                RETURN_IF_ERROR(
                    AFF4Image::OpenAFF4Image(
                        resolver, stream_urn, this, image_stream));

                result = std::move(image_stream);

                resolver->logger->debug("Openning {} as type {}",
                                        stream_urn, type_str);
                return STATUS_OK;
            }

            // The AFF4 Standard specifies an "AFF4 Image" as an abstract
            // container for image related properties. It is not actually a
            // concrete stream but it refers to a storage stream using its
            // aff4:dataStream property.

            // Note that to create such a stream, you can simply create a
            // regular stream with NewAFF4Image or NewAFF4Map and then set
            // the aff4:dataStream of a new object to a concerete Map or
            // ImageStream.
            if (type_str == AFF4_IMAGE_TYPE ||
                type_str == AFF4_DISK_IMAGE_TYPE ||
                type_str == AFF4_VOLUME_IMAGE_TYPE ||
                type_str == AFF4_MEMORY_IMAGE_TYPE ||
                type_str == AFF4_CONTIGUOUS_IMAGE_TYPE ||
                type_str == AFF4_DISCONTIGUOUS_IMAGE_TYPE) {
                URN delegate;

                if (STATUS_OK == resolver->Get(stream_urn, AFF4_DATASTREAM, delegate)) {
                    // TODO: This can get recursive. Protect against abuse.
                    return GetStream(delegate, result);
                }
            }

            if (type_str == AFF4_MAP_TYPE) {
                AFF4Flusher<AFF4Map> map_stream;
                RETURN_IF_ERROR(
                    AFF4Map::OpenAFF4Map(
                        resolver, stream_urn, this, map_stream));

                result = std::move(map_stream);
                resolver->logger->debug("Openning {} as type {}",
                                        stream_urn, type_str);

                return STATUS_OK;
            }

            // Zip segments are stored directly in each volume. We use
            // the resolver to figure out which volume has each
            // segment.
            if (type_str == AFF4_ZIP_SEGMENT_TYPE ||
                type_str == AFF4_FILE_TYPE) {
                URN owner;
                RETURN_IF_ERROR(resolver->Get(stream_urn, AFF4_STORED, owner));

                resolver->logger->debug("Openning {} as type {}", stream_urn, type_str);
                auto it = volume_objs.find(owner);
                if (it != volume_objs.end()) {
                    return (it->second->OpenMemberStream(stream_urn, result));
                }
            }
        }
    }

    // Handle symbolic streams now.
    if (stream_urn == AFF4_IMAGESTREAM_ZERO) {
        result = make_flusher<AFF4SymbolicStream>(resolver, stream_urn, 0);
        return STATUS_OK;
    }
    if (stream_urn == AFF4_IMAGESTREAM_FF) {
        result = make_flusher<AFF4SymbolicStream>(resolver, stream_urn, 0xff);
        return STATUS_OK;
    }
    if (stream_urn == AFF4_IMAGESTREAM_UNKNOWN) {
        result = make_flusher<AFF4SymbolicStream>(resolver, stream_urn, "UNKNOWN");
        return STATUS_OK;
    }
    if (stream_urn == AFF4_IMAGESTREAM_UNREADABLE) {
        result = make_flusher<AFF4SymbolicStream>(resolver, stream_urn, "UNREADABLEDATA");
        return STATUS_OK;
    }

    for (int i = 0; i < 256; i++) {
        std::string urn = aff4_sprintf(
            "%s%02X", AFF4_IMAGESTREAM_SYMBOLIC_PREFIX, i);

        if (stream_urn == urn) {
            result = make_flusher<AFF4SymbolicStream>(resolver, stream_urn, i);
            return STATUS_OK;
        }
    }

    return NOT_FOUND;
}


} // namespace aff4
