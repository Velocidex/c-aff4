#ifndef AFF4_ERRORS_H
#define AFF4_ERRORS_H

typedef enum {
  STATUS_OK = 1,
  NOT_FOUND = -1,
  INCOMPATIBLE_TYPES = -2,
  MEMORY_ERROR = -3,
  GENERIC_ERROR = -4,
  INVALID_INPUT = -5
} AFF4Status;

#endif // AFF4_ERRORS_H
