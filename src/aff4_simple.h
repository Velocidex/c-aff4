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

/**
 * @file
 * @brief This is the main include file for the AFF4 library. It presents
 *        a simple C API for creating and reading AFF4 images.
 */


/**
 * Create an AFF4 image from the source URN into the output volume. If the
 * volume does not exist, we create it, otherwise we simply insert an additional
 * stream into the old volume.
 *
 * @param output_file: The name of the file to create the volume on.
 * @param stream_name: The name of the stream to create in the volume.
 * @param chunks_per_segment: Number of chunks per segment.
 * @param max_volume_size: When we exceed this size, we create an additional
 *        volume and continue imaging to it.
 * @param input_stream: The stream to read from.
 *
 * @return
 */
AFF4Status aff4_image(char* output_file, char* stream_name,
                      unsigned int chunks_per_segment,
                      uint64_t max_volume_size,
                      AFF4Stream& input_stream);
