#ifndef AFF4_UTILS_H
#define AFF4_UTILS_H

#include <vector>

// C++ strings stop on null terminations so we can not use them to store binary
// data. We use vector<char> to represented binary data.
typedef std::vector<char> bstring;

#endif
