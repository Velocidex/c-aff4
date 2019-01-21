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
#include <unordered_map>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/sink.h>

#include "aff4/lexicon.h"
#include "aff4/libaff4.h"
#include "aff4/libaff4-c.h"


class LogSink: public spdlog::sinks::sink {
public:
    virtual ~LogSink() {
        AFF4_free_messages(head);
    }

    virtual void log(const spdlog::details::log_msg& msg) override {
        char* str = new char[msg.formatted.size()+1];
        std::strncpy(str, msg.formatted.data(), msg.formatted.size());
        str[msg.formatted.size()] = '\0';

        AFF4_Message* m = new AFF4_Message{msg.level, str, nullptr};

        if (tail) {
            tail->next = m;
        }
        else {
            head = m;
        }
        tail = m;
    }

    virtual void flush() override {}

    AFF4_Message* take() {
        AFF4_Message* msg = head;
        head = tail = nullptr;
        return msg;
    }

    void reset() {
        AFF4_free_messages(head);
        head = tail = nullptr;
    }

private:
    AFF4_Message* head = nullptr;
    AFF4_Message* tail = nullptr;
};

struct Holder {
    Holder():
        log{std::make_shared<LogSink>()},
        resolver(
            aff4::DataStoreOptions{spdlog::create("aff4", log), 1}
        )
    {}

    bool getHandle(int handle, aff4::URN& urn) {
        const auto& it = handles.find(handle);
        if (it == handles.end()) {
            return false;
        }
        urn = it->second;
        return true;
    }

    std::shared_ptr<LogSink> log;
    aff4::MemoryDataStore resolver;
    std::unordered_map<int, aff4::URN> handles;
    int nextHandle = 0;
};

Holder& get_holder() {
    static Holder the_holder;
    return the_holder;
}

const spdlog::level::level_enum LEVEL_TO_ENUM[] = {
    spdlog::level::trace,
    spdlog::level::debug,
    spdlog::level::info,
    spdlog::level::warn,
    spdlog::level::err,
    spdlog::level::critical,
    spdlog::level::off
};

extern "C" {

void AFF4_set_verbosity(unsigned int level) {
    Holder&h = get_holder();
    h.log->reset();

    const spdlog::level::level_enum e =
        level >= sizeof(LEVEL_TO_ENUM)/sizeof(LEVEL_TO_ENUM[0]) ?
        spdlog::level::off : LEVEL_TO_ENUM[level];
    h.resolver.logger->set_level(e);
    h.resolver.logger->debug(
        "Set logging level to {}", spdlog::level::to_str(e)
    );
}

AFF4_Message* AFF4_get_messages() {
    return get_holder().log->take();
}

void AFF4_free_messages(AFF4_Message* msg) {
    while (msg) {
        AFF4_Message* next = msg->next;
        delete[] msg->message;
        delete msg;
        msg = next;
    }
}

int AFF4_open(char* filename) {
    Holder& h = get_holder();
    h.log->reset();

    aff4::URN urn = aff4::URN::NewURNFromFilename(filename);
    aff4::AFF4ScopedPtr<aff4::ZipFile> zip = aff4::ZipFile::NewZipFile(
        &h.resolver, urn);
    if (!zip) {
        errno = ENOENT;
        return -1;
    }

    // Attempt AFF4 Standard, and if not, fallback to AFF4 Evimetry Legacy format.
    const aff4::URN type(aff4::AFF4_IMAGE_TYPE);
    std::unordered_set<aff4::URN> images = h.resolver.Query(aff4::AFF4_TYPE, &type);

    if (images.empty()) {
        const aff4::URN legacy_type(aff4::AFF4_LEGACY_IMAGE_TYPE);
        images = h.resolver.Query(aff4::URN(aff4::AFF4_TYPE), &legacy_type);
        if (images.empty()) {
            h.resolver.Close(zip);
            errno = ENOENT;
            return -1;
        }
    }

    // Sort URNs so that we have some sort of determinism
    std::vector<aff4::URN> sorted_images{images.begin(), images.end()};
    std::sort(sorted_images.begin(), sorted_images.end());

    // Make sure we only load an image that is stored in the filename provided
    for (const auto & image : sorted_images) {
        if (!h.resolver.HasURNWithAttributeAndValue(image, aff4::AFF4_STORED, zip->urn)) {
            continue;
        }

        if (!h.resolver.AFF4FactoryOpen<aff4::AFF4StdImage>(image)) {
            continue;
        }

        const int handle = h.nextHandle++;
        h.handles[handle] = image;
        return handle;
    }

    h.resolver.Close(zip);
    errno = ENOENT;
    return -1;
}

uint64_t AFF4_object_size(int handle) {
    Holder& h = get_holder();
    h.log->reset();

    aff4::URN urn;
    if (h.getHandle(handle, urn)) {
        auto stream = h.resolver.AFF4FactoryOpen<aff4::AFF4Stream>(urn);
        if (stream.get()) {
            return stream->Size();
        }
    }
    return 0;
}

int AFF4_read(int handle, uint64_t offset, void* buffer, int length) {
    Holder& h = get_holder();
    h.log->reset();

    aff4::URN urn;

    if (!h.getHandle(handle, urn)) return -1;

    auto stream = h.resolver.AFF4FactoryOpen<aff4::AFF4Stream>(urn);
    int read = 0;
    if (stream.get()) {
        stream->Seek(offset, SEEK_SET);
        const std::string result = stream->Read(length);
        read = result.length();
        std::memcpy(buffer, result.data(), read);
    } else {
        errno = ENOENT;
    }
    return read;
}

int AFF4_close(int handle) {
    Holder& h = get_holder();
    h.log->reset();

    aff4::URN urn;
    if (h.getHandle(handle, urn)) {
        auto obj = h.resolver.AFF4FactoryOpen<aff4::AFF4Object>(urn);
        if (obj.get()) {
            h.resolver.Close(obj);
            h.handles.erase(handle);
        }
    }
    return 0;
}

}
