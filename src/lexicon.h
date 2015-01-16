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

#ifndef     AFF4_LEXICON_H_
#define     AFF4_LEXICON_H_

#include "rdf.h"

#define AFF4_NAMESPACE "http://afflib.org/2009/aff4#"
#define XSD_NAMESPACE "http://www.w3.org/2001/XMLSchema#"
#define RDF_NAMESPACE "http://www.w3.org/1999/02/22-rdf-syntax-ns#"

static URN AFF4_TYPE(RDF_NAMESPACE "type");
static URN AFF4_STORED(AFF4_NAMESPACE "stored");


static URN AFF4_ZIP_TYPE(AFF4_NAMESPACE "zip_volume");

static URN AFF4_STREAM_SIZE(AFF4_NAMESPACE "size");

static URN AFF4_IMAGE_TYPE(AFF4_NAMESPACE "image");
static URN AFF4_IMAGE_CHUNK_SIZE(AFF4_NAMESPACE "chunk_size");
static URN AFF4_IMAGE_COMPRESSION(AFF4_NAMESPACE "compression");
static URN AFF4_IMAGE_COMPRESSION_DEFLATE("https://www.ietf.org/rfc/rfc1951.txt");

#endif
