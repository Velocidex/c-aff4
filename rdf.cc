#include "lexicon.h"
#include "rdf.h"

string RDFBytes::SerializeToString() const {
  string result;

  result.resize(value.size() * 2 + 1);

  for(unsigned int i=0; i< value.size(); i++) {
    const unsigned char c = value[i];
    result.push_back(lut[c >> 4]);
    result.push_back(lut[c & 15]);
  };

  return result;
};


AFF4Status RDFBytes::UnSerializeFromString(const char *data, int length) {
  // Length is odd.
  if (length & 1) {
    return INVALID_INPUT;
  };

  value.clear();

  for (int i = 0; i < length; i += 2) {
    char a = data[i];
    const char* p = std::lower_bound(lut, lut + 16, a);
    if (*p != a) {
      return INVALID_INPUT;
    };

    char b = data[i + 1];
    const char* q = std::lower_bound(lut, lut + 16, b);
    if (*q != b) {
      return INVALID_INPUT;
    };

    value.push_back(((p - lut) << 4) | (q - lut));
  }

  return STATUS_OK;
};


string XSDString::SerializeToString() const {
  return string(value.data(), value.size());
};

AFF4Status XSDString::UnSerializeFromString(const char *data, int length) {
  value = string(data, length);

  return STATUS_OK;
};
