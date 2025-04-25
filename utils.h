#ifndef UTILS_H
#define UTILS_H

#include <cstdlib>
#include <iostream>

#define DIE(condition, message) \
  do {                          \
    if (condition) {            \
      perror(message);          \
      exit(1);                  \
    }                           \
  } while (0)
#define ERROR(msg) fprintf(stderr, "ERROR: %s\n", msg)

#endif
