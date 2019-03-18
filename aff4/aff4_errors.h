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

#ifndef AFF4_ERRORS_H
#define AFF4_ERRORS_H

#include "aff4/config.h"

namespace aff4 {


/**
 * @file
 * @author scudette <scudette@google.com>
 * @date   Sun Jan 18 16:26:57 2015
 *
 * @brief  This file contains the error types that AFF4 objects may return.
 *
 *
 */

/// Return values from AFF4 methods and functions.
typedef enum {
    STATUS_OK = 0,                        /**< Function succeeded. */
    NOT_FOUND = -1,                       /**< URN or file was not found. */
    INCOMPATIBLE_TYPES = -2,              /**< Types passed in as arguments were
                                         * not compatible. */
    MEMORY_ERROR = -3,                    /**< Failed to allocate sufficient
                                         * memory. */
    GENERIC_ERROR = -4,                   /**< Generic Error. */
    INVALID_INPUT = -5,                   /**<  */
    PARSING_ERROR = -6,                   /**< Unable to parse the required
                                         * input. */
    NOT_IMPLEMENTED = -7,                 /**< This function is not yet
                                         * implemented. */
    IO_ERROR = -8,                        /**< Unable to open URN for IO. */
    FATAL_ERROR = -9,
    CONTINUE = -10,
    ABORTED = -11
} AFF4Status;

extern const char* AFF4StatusToString(AFF4Status status);

} // namespace aff4

#endif // AFF4_ERRORS_H
