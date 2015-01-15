#ifndef     AFF4_LEXICON_H_
#define     AFF4_LEXICON_H_

#include "rdf.h"

#define AFF4_NAMESPACE "http://afflib.org/2009/aff4#"
#define XSD_NAMESPACE "http://www.w3.org/2001/XMLSchema#"
#define RDF_NAMESPACE "http://www.w3.org/1999/02/22-rdf-syntax-ns#"

static URN AFF4_TYPE(RDF_NAMESPACE "type");
static URN AFF4_STORED(AFF4_NAMESPACE "stored");


static URN AFF4_ZIP_TYPE(AFF4_NAMESPACE "zip_volume");


static URN AFF4_IMAGE_TYPE(AFF4_NAMESPACE "image");
static URN AFF4_IMAGE_CHUNK_SIZE(AFF4_NAMESPACE "chunk_size");

#endif
