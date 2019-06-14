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

#ifndef LIBAFF4_C_H_
#define LIBAFF4_C_H_

#include <stdint.h>

/*
 * This is the C interface into libaff4.
 *
 * Note: Individual handles are NOT MT-SAFE.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the AFF4 version
 */
const char* AFF4_version();

/**
 * The message struct. Follow the next pointer for the next message; the last
 * message will have a null next pointer. Every AFF4_Message* produced by the
 * C API MUST be freed by AFF4_free_messages().
 */
typedef struct AFF4_Message {
    unsigned int level;
    char* message;
    struct AFF4_Message* next;
} AFF4_Message;

/**
 * Free message list.
 * @param msg The message pointer
 */
void AFF4_free_messages(AFF4_Message* msg);

/**
 * Log verbosity levels
 */
typedef enum {
    AFF4_LOG_LEVEL_TRACE,
    AFF4_LOG_LEVEL_DEBUG,
    AFF4_LOG_LEVEL_INFO,
    AFF4_LOG_LEVEL_WARNING,
    AFF4_LOG_LEVEL_ERROR,
    AFF4_LOG_LEVEL_CRITICAL,
    AFF4_LOG_LEVEL_OFF
} AFF4_LOG_LEVEL;

/**
 * Set the verbosity level for logging.
 *
 * @param level The verbosity level
 */
void AFF4_set_verbosity(AFF4_LOG_LEVEL level);

typedef struct AFF4_Handle AFF4_Handle;

/**
 * Open the given filename, and access the first aff4:Image in the container.
 * @param filename The filename to open.
 * @param msg A pointer to log messages.
 * @return Object handle, or NULL on error. See errno
 */
AFF4_Handle* AFF4_open(const char* filename, AFF4_Message** msg);

/**
 * Get the size of the AFF4 Object that was opened.
 */
uint64_t AFF4_object_size(AFF4_Handle* handle, AFF4_Message** msg);

/**
 * Read a block from the given handle.
 * @param handle The Object handle.
 * @param offset the offset into the stream
 * @param buffer Pointer to a buffer of length.
 * @param length The length of the buffer to fill.
 * @param msg A pointer to log messages.
 * @return The number of bytes placed into the buffer.
 */
ssize_t AFF4_read(AFF4_Handle* handle, uint64_t offset, void* buffer, size_t length, AFF4_Message** msg);

/**
 * Close the given handle.
 * @param handle The Object handle to close.
 * @param msg A pointer to log messages.
 * @return 0, or -1 on error.
 */
int AFF4_close(AFF4_Handle* handle, AFF4_Message** msg);

#ifdef __cplusplus
}
#endif

#endif /* LIBAFF4_C_H_ */
