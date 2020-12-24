#include "Python.h"
#include <stdio.h>

#define ASSERT(assertion...)                                                   \
  do {                                                                         \
    if (!(assertion)) {                                                        \
      asprintf(&failure_msg,                                                   \
               "Assertion failed on line %d in %s (function %s):\n%s",         \
               __LINE__,                                                       \
               __FILE__,                                                       \
               __func__,                                                       \
               #assertion);                                                    \
      return failure_msg;                                                      \
    }                                                                          \
  } while (0)
