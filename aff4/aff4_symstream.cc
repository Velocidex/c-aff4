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

    // FIXME: move to base class
    std::string AFF4SymbolicStream::Read(size_t length) {
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

    void AFF4SymbolicStream::Return() {
        // Don't return to the resolver as we are a permanent entity.
        //resolver->Return(this);
    }

} // namespace aff4
