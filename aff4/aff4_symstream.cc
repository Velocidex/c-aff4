/*
 * AFF4SymbolicStream.cc
 *
 *  Created on: 21Oct.,2016
 *      Author: darran
 */

#include "aff4/aff4_symstream.h"

namespace aff4 {


    AFF4SymbolicStream::AFF4SymbolicStream(DataStore* dataStore, URN urn,
                                           uint8_t symbol) :
        AFF4Stream(dataStore, urn), symbol(symbol), pattern("") {
        size = LLONG_MAX;
    }

    AFF4SymbolicStream::AFF4SymbolicStream(DataStore* dataStore, URN urn,
                                           std::string pattern) :
        AFF4Stream(dataStore, urn), symbol(0), pattern(pattern) {
        size = LLONG_MAX;
    }

    AFF4SymbolicStream::~AFF4SymbolicStream() {
        // NOP
    }

    AFF4Status AFF4SymbolicStream::ReadBuffer(char* data, size_t* length) {
        if (pattern.empty()) {
            // fill with symbol
            std::memset(data, symbol, *length);
            readptr += *length;
            // cycle around to zero if we go past the logical end of
            // the stream.
            if (readptr < 0) {
                readptr = 0;
            }
        } else {
            // fill with pattern
            const size_t pSz = pattern.size();
            size_t toRead = *length;
            while (toRead > 0) {
                int pOffset = readptr % pSz;
                *data = pattern[pOffset];
                ++data;
                --toRead;

                ++readptr;
                // cycle around to zero if we go past the logical
                // end of the stream.
                if (readptr < 0) {
                    readptr = 0;
                }
            }
        }
        return STATUS_OK;
    }
} // namespace aff4
