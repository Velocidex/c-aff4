/*
 * AFF4SymbolicStream.cc
 *
 *  Created on: 21Oct.,2016
 *      Author: darran
 */

#include "aff4_symstream.h"

AFF4SymbolicStream::AFF4SymbolicStream(DataStore* dataStore, URN urn, uint8_t symbol) :
		AFF4Stream(dataStore, urn), symbol(symbol), pattern("") {
	size = LLONG_MAX;
}

AFF4SymbolicStream::AFF4SymbolicStream(DataStore* dataStore, URN urn, std::string pattern) :
		AFF4Stream(dataStore, urn), symbol(0), pattern(pattern) {
	size = LLONG_MAX;
}

AFF4SymbolicStream::~AFF4SymbolicStream() {
	// NOP
}

std::string AFF4SymbolicStream::Read(size_t length) {
	std::string result;
	result.resize(length);
	if (pattern.empty()) {
		// fill with symbol
		std::memset((void*) result.data(), symbol, length);
		readptr += length;
		// cycle around to zero if we go passed the logical end of the stream.
		if (readptr < 0) {
			readptr = 0;
		}
	} else {
		// fill with pattern
		const size_t pSz = pattern.size();
		char* data = const_cast<char*>(result.data());
		size_t toRead = length;
		while (toRead > 0) {
			int pOffset = readptr % pSz;
			*data = pattern[pOffset];
			data++;
			toRead--;

			readptr++;
			// cycle around to zero if we go passed the logical end of the stream.
			if (readptr < 0) {
				readptr = 0;
			}
		}
	}
	return result;
}

void AFF4SymbolicStream::Return() {
	// Don't return to the resolver as we are a permanent entity.
    //resolver->Return(this);
}

