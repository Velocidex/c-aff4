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


static std::shared_ptr<spdlog::logger> setup_c_api_logger() {
    spdlog::drop(aff4::LOGGER);
    auto logger = spdlog::create(aff4::LOGGER, std::make_shared<LogSink>());
    logger->set_level(spdlog::level::err);
    return logger;
}

static std::shared_ptr<spdlog::logger> get_c_api_logger() {
    static std::shared_ptr<spdlog::logger> the_logger = setup_c_api_logger();
    return the_logger;
}

struct AFF4_Handle {
    aff4::MemoryDataStore resolver;
    aff4::URN urn;
    aff4::VolumeGroup volumes;
    aff4::AFF4Flusher<aff4::AFF4Stream> stream;
    std::string filename;

    AFF4_Handle(const std::string & filename):
        resolver(aff4::DataStoreOptions(get_c_api_logger(), 1)),
        volumes(&resolver),
        filename(filename)
    {}

  protected:
    bool open() {
        aff4::AFF4Flusher<aff4::FileBackedObject> file;
        if (aff4::STATUS_OK != aff4::NewFileBackedObject(
                &resolver, filename, "read", file)) {
            return false;
        }

        aff4::AFF4Flusher<aff4::AFF4Volume> zip;
        if (aff4::STATUS_OK != aff4::ZipFile::OpenZipFile(
                &resolver, aff4::AFF4Flusher<aff4::AFF4Stream>(file.release()), zip)) {
            return false;
        };

        volumes.AddVolume(std::move(zip));

        // Attempt AFF4 Standard, and if not, fallback to AFF4 Evimetry Legacy format.

        const aff4::URN type(aff4::AFF4_IMAGE_TYPE);
        auto images = resolver.Query(aff4::AFF4_TYPE, &type);

        if (images.empty()) {
            const aff4::URN legacy_type(aff4::AFF4_LEGACY_IMAGE_TYPE);
            images = resolver.Query(aff4::URN(aff4::AFF4_TYPE), &legacy_type);
            if (images.empty()) {
                return false;
            }
        }

        // For determinism, get the "first" sorted urn in the set
        urn = *std::min_element(images.begin(), images.end());

        if (aff4::STATUS_OK != volumes.GetStream(urn, stream)) {
            return false;
        }

        return true;
    }

    friend class HandlePool;
};

class HandlePool {
    using pool_type = std::vector<std::unique_ptr<AFF4_Handle>>;

  public:

    AFF4_Handle * get(const std::string & filename) {
        std::lock_guard<std::mutex> lock{pool_lock};

        const auto it = std::find_if(pool.begin(), pool.end(),
            [&filename](const std::unique_ptr<AFF4_Handle> & el) {
                return (el != nullptr && el->filename == filename);
            });

        if (it != pool.end()) {
            return it->release();
        }

        std::unique_ptr<AFF4_Handle> h(new AFF4_Handle(filename));

        if (h->open()) {
            return h.release();
        }

        return nullptr;
    }

    void put(AFF4_Handle * h) {
        std::lock_guard<std::mutex> lock{pool_lock};

        if (pool.empty()) {
            delete h;
            return;
        }

        if (next == pool.end()) {
            next = pool.begin();
        }

        next->reset(h);
        next++;
    }

    void set_cache_size(size_t n) {
        std::lock_guard<std::mutex> lock{pool_lock};

        pool.resize(n);
        next = pool.begin();
    }

    void clear_cache() {
        std::lock_guard<std::mutex> lock{pool_lock};

        const auto n = pool.size();

        pool.clear();
        pool.resize(n);
        next = pool.begin();
    }

  private:
    std::mutex pool_lock{};
    pool_type pool{};
    pool_type::iterator next{pool.begin()};
};

static HandlePool & handle_pool() {
    static HandlePool pool{};
    return pool;
}

static spdlog::level::level_enum enum_for_level(AFF4_LOG_LEVEL level) {
    switch (level) {
        case AFF4_LOG_LEVEL_TRACE:
            return spdlog::level::trace;
        case AFF4_LOG_LEVEL_DEBUG:
            return spdlog::level::debug;
        case AFF4_LOG_LEVEL_INFO:
            return spdlog::level::info;
        case AFF4_LOG_LEVEL_WARNING:
            return spdlog::level::warn;
        case AFF4_LOG_LEVEL_ERROR:
            return spdlog::level::err;
        case AFF4_LOG_LEVEL_CRITICAL:
            return spdlog::level::critical;
        case AFF4_LOG_LEVEL_OFF:
            return spdlog::level::off;
    }

    // Should be unreachable
    return spdlog::level::err;
}

extern "C" {

void AFF4_set_verbosity(AFF4_LOG_LEVEL level) {
    get_c_api_logger()->set_level(enum_for_level(level));
}

void AFF4_set_handle_cache_size(size_t n) {
    handle_pool().set_cache_size(n);
}

void AFF4_clear_handle_cache() {
    handle_pool().clear_cache();
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
    get_log_handler().use(msg);

    AFF4_Handle * h = handle_pool().get(filename);

    if (h == nullptr) {
        errno = ENOENT;
    }

    return h;
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

    handle_pool().put(handle);

    return 0;
}

int AFF4_get_boolean_property(AFF4_Handle* handle, const char * property, int* result, AFF4_Message** msg) {
  if (!handle || !result) {
    errno = EINVAL;
    return -1;
  }

  get_log_handler().use(msg);

  aff4::XSDBoolean value;
  if (handle->resolver.Get(handle->urn, aff4::URN(property), value) != aff4::STATUS_OK) {
      errno = ENOENT;
      return -1;
  }

  *result = (value.value) ? 1 : 0;

  return 0;
}

int AFF4_get_integer_property(AFF4_Handle* handle, const char * property, int64_t* result, AFF4_Message** msg) {
  if (!handle || !result) {
    errno = EINVAL;
    return -1;
  }

  get_log_handler().use(msg);

  aff4::XSDInteger value;
  if (handle->resolver.Get(handle->urn, aff4::URN(property), value) != aff4::STATUS_OK) {
      errno = ENOENT;
      return -1;
  }

  *result = value.value;

  return 0;
}

int AFF4_get_string_property(AFF4_Handle* handle, const char * property, char** result, AFF4_Message** msg) {
  if (!handle || !result) {
    errno = EINVAL;
    return -1;
  }

  *result = nullptr;

  get_log_handler().use(msg);

  aff4::XSDString value;
  if (handle->resolver.Get(handle->urn, aff4::URN(property), value) != aff4::STATUS_OK) {
      errno = ENOENT;
      return -1;
  }

  const std::string & res = value.value;

  *result = (char *) malloc(res.length() + 1);
  if (*result == nullptr) {
      errno = ENOMEM;
      return -1;
  }

  res.copy(*result, res.length());
  *result[res.length()] = 0; // null terminate string

  return 0;
}

int AFF4_get_binary_property(AFF4_Handle* handle, const char * property, AFF4_Binary_Result* result, AFF4_Message** msg) {
  if (!handle || !result) {
    errno = EINVAL;
    return -1;
  }

  *result = {};

  get_log_handler().use(msg);

  aff4::RDFBytes value;
  if (handle->resolver.Get(handle->urn, aff4::URN(property), value) != aff4::STATUS_OK) {
      errno = ENOENT;
      return -1;
  }

  const std::string & res = value.value;

  result->data = malloc(res.length());
  if (result->data == nullptr) {
      errno = ENOMEM;
      return -1;
  }
  result->length = res.length();

  res.copy(static_cast<char *>(result->data), res.length());

  return 0;
}

}
