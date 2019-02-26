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

#include <cstring>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/sink.h>

#include "aff4/aff4_errors.h"
#include "aff4/lexicon.h"
#include "aff4/libaff4.h"
#include "aff4/libaff4-c.h"


class LogHandler {
public:
    void log(const spdlog::details::log_msg& msg) {
        if (!head) {
            // C API function was called with null msg, so don't log messages
            return;
        }

        // populate our message struct
        char* str = new char[msg.raw.size()+1];
        std::strncpy(str, msg.raw.data(), msg.raw.size());
        str[msg.raw.size()] = '\0';

        AFF4_Message* m = new AFF4_Message{msg.level, str, nullptr};

        // append it to the list
        if (tail) {
            tail->next = m;
        }
        else {
            *head = m;
        }
        tail = m;
    }

    void use(AFF4_Message** msg) {
      // set where to store messages, if any
      head = msg;
      tail = nullptr;
    }

private:
    AFF4_Message** head = nullptr;
    AFF4_Message* tail = nullptr;
};

LogHandler& get_log_handler() {
    static thread_local LogHandler log_handler;
    return log_handler;
}

class LogSink: public spdlog::sinks::sink {
public:
    virtual ~LogSink() {}

    void log(const spdlog::details::log_msg& msg) override {
        // trampoline to our thread-local log handler
        get_log_handler().log(msg);
    }

    void flush() override {}
};


std::shared_ptr<spdlog::logger> setup_c_api_logger() {
    spdlog::drop(aff4::LOGGER);
    auto logger = spdlog::create(aff4::LOGGER, std::make_shared<LogSink>());
    logger->set_level(spdlog::level::err);
    return logger;
}

std::shared_ptr<spdlog::logger> get_c_api_logger() {
    static std::shared_ptr<spdlog::logger> the_logger = setup_c_api_logger();
    return the_logger;
}

struct AFF4_Handle {
    aff4::MemoryDataStore resolver;
    aff4::URN urn;
    aff4::VolumeGroup volumes;
    aff4::AFF4Flusher<aff4::AFF4Stream> stream;

    AFF4_Handle():
        resolver(aff4::DataStoreOptions(get_c_api_logger(), 1)),
        volumes(&resolver)
    {}
};

spdlog::level::level_enum enum_for_level(unsigned int level) {
    switch (level) {
    case 0:
        return spdlog::level::trace;
    case 1:
        return spdlog::level::debug;
    case 2:
        return spdlog::level::info;
    case 3:
        return spdlog::level::warn;
    default:
        return spdlog::level::err;
    }
}

extern "C" {

void AFF4_init() {}

void AFF4_set_verbosity(unsigned int level) {
    get_c_api_logger()->set_level(enum_for_level(level));
}

void AFF4_free_messages(AFF4_Message* msg) {
    while (msg) {
        AFF4_Message* next = msg->next;
        delete[] msg->message;
        delete msg;
        msg = next;
    }
}

AFF4_Handle* AFF4_open(const char* filename, AFF4_Message** msg) {
    std::unique_ptr<AFF4_Handle> h(new AFF4_Handle());
    std::unordered_set<aff4::URN> images;
    const aff4::URN type(aff4::AFF4_IMAGE_TYPE);
    aff4::AFF4Flusher<aff4::AFF4Volume> zip;

    get_log_handler().use(msg);

    aff4::AFF4Flusher<aff4::FileBackedObject> file;
    if (aff4::STATUS_OK != aff4::NewFileBackedObject(
            &h->resolver, filename, "read", file)) {
        goto fail;
    }

    if (aff4::STATUS_OK != aff4::ZipFile::OpenZipFile(
            &h->resolver, aff4::AFF4Flusher<aff4::AFF4Stream>(file.release()), zip)) {
        goto fail;
    };

    h->volumes.AddVolume(std::move(zip));

    // Attempt AFF4 Standard, and if not, fallback to AFF4 Evimetry Legacy format.
    images = h->resolver.Query(aff4::AFF4_TYPE, &type);

    if (images.empty()) {
        const aff4::URN legacy_type(aff4::AFF4_LEGACY_IMAGE_TYPE);
        images = h->resolver.Query(aff4::URN(aff4::AFF4_TYPE), &legacy_type);
        if (images.empty()) {
            goto fail;
        }
    }

    {
        // Sort URNs so that we have some sort of determinism
        std::vector<aff4::URN> sorted_images{images.begin(), images.end()};
        std::sort(sorted_images.begin(), sorted_images.end());

        for (const auto& img_urn: sorted_images) {
            aff4::AFF4Flusher<aff4::AFF4Stream> image;
            if (aff4::STATUS_OK != h->volumes.GetStream(img_urn, image)) {
                goto fail;
            }

            h->stream.reset(image.release());

            return h.release();
        }
    }

 fail:
    errno = ENOENT;
    return nullptr;
}

uint64_t AFF4_object_size(AFF4_Handle* handle, AFF4_Message** msg) {
    get_log_handler().use(msg);

    if (handle) {
        return handle->stream->Size();
    }
    return 0;
}

ssize_t AFF4_read(AFF4_Handle* handle, uint64_t offset,
                  void* buffer, size_t length, AFF4_Message** msg) {
    get_log_handler().use(msg);

    if (!handle) {
        return -1;
    }

    if (!handle->stream) {
        errno = ENOENT;
        return -1;
    }

    if (handle->stream->Seek(offset, SEEK_SET) != aff4::STATUS_OK ||
        handle->stream->ReadBuffer(static_cast<char*>(buffer), &length) != aff4::STATUS_OK) {
        errno = EIO;
        return -1;
    }

    return length;
}

int AFF4_close(AFF4_Handle* handle, AFF4_Message** msg) {
    get_log_handler().use(msg);

    if (handle) {
        handle->stream.release();
    }
    return 0;
}

}
