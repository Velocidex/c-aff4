/*
 * AFF4SymbolicStream.h
 *
 *  Created on: 21Oct.,2016
 *      Author: darran
 */

#ifndef SRC_AFF4_SYMSTREAM_H_
#define SRC_AFF4_SYMSTREAM_H_

#include "aff4/aff4_io.h"

namespace aff4 {

#ifndef AFF4Stream
class AFF4Stream;
#endif

class AFF4SymbolicStream: public AFF4Stream {
 public:
    AFF4SymbolicStream(DataStore* dataStore, URN urn, uint8_t symbol);

    AFF4SymbolicStream(DataStore* dataStore, URN urn, std::string pattern);

    virtual ~AFF4SymbolicStream();

    std::string Read(size_t length);

    // Override AFF4Object::Return();
    void Return();

 protected:
    uint8_t symbol;
    std::string pattern;
};

} // namespace aff4

#endif /* SRC_AFF4_SYMSTREAM_H_ */
