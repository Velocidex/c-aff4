/*
Copyright 2014 Google Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use
this file except in compliance with the License.  You may obtain a copy of the
License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.  See the License for the
specific language governing permissions and limitations under the License.
*/

#ifdef _WIN32
#include "shlwapi.h"
#else
#include <fnmatch.h>
#endif

#include "aff4/config.h"

#include "aff4/aff4_utils.h"
#include "aff4/aff4_errors.h"
#include "aff4/aff4_io.h"
#include "aff4/libaff4.h"
#include "aff4/aff4_directory.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <cstring>
#include <iostream>
#include <iomanip>

namespace aff4 {

// Flip to true to immediately stop operations.
bool aff4_abort_signaled = false;

#if defined(HAVE_LIBUUID)
#include <uuid/uuid.h>

/**
 * By default empty objects receive a unique URN.
 *
 * @param resolver
 */
AFF4Object::AFF4Object(DataStore* resolver): resolver(resolver) {
    uuid_t uuid;
    char buffer[100];

    memset(buffer, 0, 100);

    uuid_generate(uuid);
    uuid_unparse_lower(uuid, buffer);
    urn.Set(AFF4_PREFIX + std::string(buffer));
}

// On windows we use native GUID generation.
#elif defined(_WIN32)
#include <objbase.h>

/**
 * By default empty objects receive a unique URN.
 *
 * @param resolver
 */
AFF4Object::AFF4Object(DataStore* resolver): resolver(resolver) {
    GUID newId;
    UuidCreate(&newId);

    unsigned char* buffer;
    UuidToString(&newId, &buffer);

    urn.Set(AFF4_PREFIX + std::string(reinterpret_cast<char*>(buffer)));

    RpcStringFree(&buffer);
}

#endif

AFF4Status AFF4Object::Flush() {
    // Flushing makes the object no longer dirty.
    _dirty = false;

    return STATUS_OK;
}

bool AFF4Stream::CanSwitchVolume() {
    return false;
}

AFF4Status AFF4Stream::SwitchVolume(AFF4Volume *volume) {
    UNUSED(volume);
    return NOT_IMPLEMENTED;
}

AFF4Status AFF4Stream::Seek(off_t offset, int whence) {
    if (!properties.seekable)
        return IO_ERROR;

    off_t new_offset = readptr;

    if (whence == 0) {
        new_offset = offset;

    } else if (whence == 1) {
        new_offset += offset;

    } else if (whence == 2) {
        // We cannot seek relative to size for streams which are non sizeable.
        if (!properties.sizeable) {
            return IO_ERROR;
        }

        new_offset = offset + Size();
    }

    if (new_offset < 0) {
        new_offset = 0;
    }

    // For non-seekable streams its ok to seek to the current position.
    if (!properties.seekable && new_offset != offset) {
        return IO_ERROR;
    }

    readptr = new_offset;
    return STATUS_OK;
}

std::string AFF4Stream::Read(size_t length) {
    if (length == 0) {
        return "";
    }

    std::string result(length, '\0');
    if (ReadBuffer(&result[0], &length) != STATUS_OK) {
        return "";
    }
    result.resize(length);
    return result;
}

AFF4Status AFF4Stream::ReadBuffer(char* data, size_t *length) {
    UNUSED(data);
    *length = 0;
    return STATUS_OK;
}

AFF4Status AFF4Stream::Write(const std::string& data) {
    return Write(data.c_str(), data.size());
}

AFF4Status AFF4Stream::Write(const char data[]) {
    return Write(data, strlen(data));
}

AFF4Status AFF4Stream::Write(const char* data, size_t length) {
    UNUSED(data);
    UNUSED(length);
    return NOT_IMPLEMENTED;
}

int AFF4Stream::ReadIntoBuffer(void* buffer, size_t length) {
    // FIXME: errors?
    ReadBuffer(reinterpret_cast<char*>(buffer), &length);
    return length;
}

off_t AFF4Stream::Tell() {
    return readptr;
}

off_t AFF4Stream::Size() const {
    return size;
}

void AFF4Stream::reserve(size_t size) {
    UNUSED(size);
}


bool DefaultProgress::Report(aff4_off_t readptr) {
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    std::chrono::duration<double> delta = std::chrono::duration_cast<std::chrono::duration<double>>(now - last_time);

    // At least 1/4 second has elapsed
    if (delta.count() >= 0.25 ) {
        total_read += readptr - last_offset;

        // Rate in MB/s.
        double rate = (double)(readptr - last_offset) / (1024.0*1024.0)
                     / delta.count();

        if (length > 0) {
            resolver->logger->info(
                " Reading {:x} {} MiB / {} ({:.0f} MiB/s)",
                readptr, total_read/1024/1024,
                length/1024/1024, rate);
        } else {
            resolver->logger->info(
                " Reading {:x} {} MiB ({:.0f} MiB/s)", readptr,
                total_read/1024/1024, rate);
        }
        last_time = now;
        last_offset = readptr;
    }

    if (aff4_abort_signaled) {
        resolver->logger->critical("Aborted!");
        return false;
    }

    return true;
}

AFF4Status AFF4Stream::CopyToStream(
    AFF4Stream& output, aff4_off_t length,
    ProgressContext* progress, size_t buffer_size) {
    DefaultProgress default_progress(resolver);
    if (!progress) {
        progress = &default_progress;
    }

    aff4_off_t length_remaining = length;

    while (length_remaining > 0) {
        size_t to_read = std::min((aff4_off_t)buffer_size, length_remaining);
        std::string data = Read(to_read);
        if (data.size() == 0) {
            break;
        }
        length_remaining -= data.size();

        RETURN_IF_ERROR(output.Write(data));
        if (!progress->Report(readptr)) {
            return ABORTED;
        }
    }

    return STATUS_OK;
}

AFF4Status AFF4Stream::WriteStream(AFF4Stream* source,
                                   ProgressContext* progress) {
    DefaultProgress default_progress(resolver);
    if (!progress) {
        progress = &default_progress;
    }

    // Rewind the source to the start.
    source->Seek(0, SEEK_SET);

    char buffer[AFF4_BUFF_SIZE];
    while (1) {
        size_t length = AFF4_BUFF_SIZE;
        RETURN_IF_ERROR(source->ReadBuffer(buffer, &length));
        if (length == 0) {
            break;
        }

        RETURN_IF_ERROR(Write(buffer, length));

        // Report the data read from the source.
        if (!progress->Report(source->Tell())) {
            return ABORTED;
        }
    }

    return STATUS_OK;
}

std::string aff4_sprintf(std::string fmt, ...) {
    va_list ap;
    int size = fmt.size() * 2 + 50;

    while (1) {
        std::unique_ptr<char[]> buffer(new char[size + 1]);

        // Null terminate the buffer (important on MSVC which does not always
        // terminate).
        buffer.get()[size] = 0;

        va_start(ap, fmt);
        int n = vsnprintf(buffer.get(), size, fmt.c_str(), ap);
        va_end(ap);

        if (n > -1 && n < size) {  // Everything worked
            return std::string(buffer.get(), n);
        }

        if (n > -1) { // Needed size returned
            size = n + 1;    // For null char
        } else {
            size *= 2;    // Guess at a larger size (OS specific)
        }
    }
}


int AFF4Stream::sprintf(std::string fmt, ...) {
    va_list ap;
    int size = fmt.size() * 2 + 50;

    while (1) {
        char* buffer = new char[size + 1];

        // Null terminate the buffer (important on MSVC which does not always
        // terminate).
        buffer[size] = 0;

        va_start(ap, fmt);
        int n = vsnprintf(buffer, size, fmt.c_str(), ap);
        va_end(ap);

        if (n > -1 && n < size) {  // Everything worked
            Write(buffer, n);
            delete[] buffer;
            return n;
        }
        delete[] buffer;
        if (n > -1) { // Needed size returned
            size = n + 1;    // For null char
        } else {
            size *= 2;    // Guess at a larger size (OS specific)
        }
    }
}

AFF4Status StringIO::Write(const char* data, size_t length) {
    MarkDirty();

    buffer.replace(readptr, length, data, length);
    readptr += length;

    size = std::max(size, readptr);

    return STATUS_OK;
}

std::string StringIO::Read(size_t length) {
    std::string result = buffer.substr(readptr, length);
    readptr += result.size();
    return result;
}

AFF4Status StringIO::ReadBuffer(char* data, size_t* length) {
    *length = std::min((aff4_off_t)*length, (aff4_off_t)(buffer.size() - readptr));
    std::memcpy(data, buffer.data() + readptr, *length);
    readptr += *length;
    return STATUS_OK;
}

off_t StringIO::Size() const {
    return buffer.size();
}

AFF4Status StringIO::Truncate() {
    buffer = "";
    readptr = 0;
    return STATUS_OK;
}

void StringIO::reserve(size_t size) {
    buffer.reserve(size);
}

aff4_off_t AFF4Volume::Size() const {
    return 0;
}


ClassFactory<AFF4Object>* GetAFF4ClassFactory() {
    static auto* factory = new ClassFactory<AFF4Object>();
    return factory;
}


#ifdef _WIN32

std::string GetLastErrorMessage() {
    LPTSTR lpMsgBuf;
    DWORD dw = GetLastError();

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR) &lpMsgBuf,
        0, nullptr);

    return lpMsgBuf;
}

#else
std::string GetLastErrorMessage() {
    return std::strerror(errno);
}

#endif




extern "C" {
    const char* AFF4_version() {
        static const std::string version = std::string("libaff4 version ") + AFF4_VERSION;
        return version.c_str();
    }
}

const char* AFF4StatusToString(AFF4Status status) {
    switch (status) {
    case STATUS_OK: return "STATUS_OK";
    case NOT_FOUND: return "NOT_FOUND";
    case INCOMPATIBLE_TYPES: return "INCOMPATIBLE_TYPES";
    case MEMORY_ERROR: return "MEMORY_ERROR";
    case GENERIC_ERROR: return "GENERIC_ERROR";
    case INVALID_INPUT: return "INVALID_INPUT";
    case PARSING_ERROR: return "PARSING_ERROR";
    case NOT_IMPLEMENTED: return "NOT_IMPLEMENTED";
    case IO_ERROR: return "IO_ERROR";
    case FATAL_ERROR: return "FATAL_ERROR";
    case ABORTED: return "ABORTED";
    default: return "UNKNOWN";
    };
}

AFF4_IMAGE_COMPRESSION_ENUM CompressionMethodFromURN(URN method) {
    if (method.value == AFF4_IMAGE_COMPRESSION_ZLIB) {
        return AFF4_IMAGE_COMPRESSION_ENUM_ZLIB;
    } else if (method.value == AFF4_IMAGE_COMPRESSION_DEFLATE) {
        return AFF4_IMAGE_COMPRESSION_ENUM_DEFLATE;
    } else if (method.value == AFF4_IMAGE_COMPRESSION_SNAPPY) {
        return AFF4_IMAGE_COMPRESSION_ENUM_SNAPPY;
    } else if (method.value == AFF4_IMAGE_COMPRESSION_SNAPPY2) {
            return AFF4_IMAGE_COMPRESSION_ENUM_SNAPPY;
    } else if (method.value == AFF4_IMAGE_COMPRESSION_LZ4) {
            return AFF4_IMAGE_COMPRESSION_ENUM_LZ4;
    } else if (method.value == AFF4_IMAGE_COMPRESSION_STORED) {
        return AFF4_IMAGE_COMPRESSION_ENUM_STORED;
    } else if (method.value == AFF4_LEGACY_IMAGE_COMPRESSION_STORED) {
           return AFF4_IMAGE_COMPRESSION_ENUM_STORED;
    } else {
        return AFF4_IMAGE_COMPRESSION_ENUM_UNKNOWN;
    }
}

URN CompressionMethodToURN(AFF4_IMAGE_COMPRESSION_ENUM method) {
    switch (method) {
        case AFF4_IMAGE_COMPRESSION_ENUM_ZLIB:
            return AFF4_IMAGE_COMPRESSION_ZLIB;

        case AFF4_IMAGE_COMPRESSION_ENUM_DEFLATE:
            return AFF4_IMAGE_COMPRESSION_DEFLATE;

        case AFF4_IMAGE_COMPRESSION_ENUM_SNAPPY:
            return AFF4_IMAGE_COMPRESSION_SNAPPY;

        case AFF4_IMAGE_COMPRESSION_ENUM_LZ4:
            return AFF4_IMAGE_COMPRESSION_LZ4;

        case AFF4_IMAGE_COMPRESSION_ENUM_STORED:
            return AFF4_IMAGE_COMPRESSION_STORED;

        default:
            return "";
    }
}


// Utilities.

std::string escape_component(std::string filename) {
    std::stringstream result;

    for (unsigned int i = 0; i < filename.size(); i++) {
        char j = filename[i];
        if ((j == '!' || j == '$' ||
             j == '\\' || j == ':' || j == '*' || j == '%' ||
             j == '?' || j == '"' || j == '<' || j == '>' || j == '|')) {
            result << "%" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') <<
                static_cast<int>(j);
            continue;
        }

        // Escape // sequences.
        if (filename[i] == '/' && i < filename.size()-1 &&
                filename[i+1] == '/') {
            result << "%" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') <<
                   static_cast<int>(filename[i]);

            result << "%" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') <<
                   static_cast<int>(filename[i+1]);
            i++;
            continue;
        }

        result << j;
    }

    return result.str();
}

std::string join(const std::vector<std::string>& v, char c) {
    std::string s;
    for (auto p = v.begin(); p != v.end(); ++p) {
        s += *p;
        if (p != v.end() - 1)
            s += c;
    }

    return s;
}


// Convert a member's name into a URN. NOTE: This function is not
// unicode safe and may mess up the zip segment name if the URN
// contains unicode chars. Ultimately it does not matter as the member
// name is just a convenience to the URN.
std::string member_name_for_urn(const URN member, const URN base_urn,
                                bool slash_ok) {
    std::string filename = base_urn.RelativePath(member);

    if (slash_ok) {
        std::vector<std::string> components;;
        for (auto &c: break_path_into_components(filename)) {
            auto escaped = escape_component(c);
            if (escaped.size() > 0) {
                components.push_back(escaped);
            }
        }

        return join(components, '/');
    }

    // Make sure zip members do not have leading /.
    while (filename.size() > 0 && filename[0] == '/') {
        filename = filename.substr(1, filename.size());
    }

    // Now escape any chars which are forbidden.
    return escape_component(filename);
}

URN urn_from_member_name(const std::string& member, const URN base_urn) {
    std::string result;

    // Now escape any chars which are non printable.
    for (unsigned int i = 0; i < member.size(); i++) {
        if (member[i] == '%') {
            i++;

            int number = std::stoi(member.substr(i, 2), nullptr, 16);
            if (number) {
                result += static_cast<char>(number);
            }

            // We consume 2 chars.
            i++;
        } else {
            result += member[i];
        }
    }

    // If this is a fully qualified AFF4 URN we return it as is, else we return
    // the relative URN to our base.
    if (result.substr(0, strlen("aff4:")) == "aff4:") {
        return result;
    }

    return base_urn.Append(result);
}

std::vector<std::string> break_path_into_components(std::string path) {
    std::vector<std::string> result;
    for(int i = 0; i < path.size(); i++) {
        // An aff4:// at the start is special and must be encoded
        // together with the first component.
        if (i==0 && path.substr(0, 7) == "aff4://") {
            int first_slash = path.find_first_of("/\\", 8);
            if (first_slash == -1) {
                result.push_back(path);
                break;
            }

            result.push_back(path.substr(0, first_slash));
            i = first_slash;
            continue;
        }

        int first_slash = path.find_first_of("/\\", i);
        if (first_slash == -1) {
            // No more slashes.
            result.push_back(path.substr(i, path.size() - i));
            break;
        }

        auto substr_len = first_slash - i;
        if (substr_len > 0) {
            result.push_back(path.substr(i, first_slash-i));
        }

        i = first_slash;
    }

    return result;
}

std::vector<std::string>& split(const std::string& s, char delim,
                                std::vector<std::string>& elems) {
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> elems;
    split(s, delim, elems);
    return elems;
}

std::shared_ptr<spdlog::logger> get_logger() {
    auto logger = spdlog::get(aff4::LOGGER);

    if (!logger) {
        if (!spdlog::details::os::in_terminal(stderr)) {
            return spdlog::stderr_logger_mt(aff4::LOGGER);
        }
        return spdlog::stderr_color_mt(aff4::LOGGER);
    }

    return logger;
}


#ifndef FNM_EXTMATCH
#define FNM_EXTMATCH 0
#endif

#ifdef _WIN32

int fnmatch(const char *pattern, const char *string) {
    return !PathMatchSpec(string, pattern);
}

#else

int fnmatch(const char *pattern, const char *string) {
    return ::fnmatch(pattern, string, FNM_EXTMATCH | FNM_CASEFOLD);
}

#endif

} // namespace aff4
