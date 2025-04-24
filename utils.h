#ifndef UTILS_H
#define UTILS_H


#include <iostream>
#include <cstdlib>

#define DIE(condition, message) do { if (condition) { perror(message); exit(1); } } while (0)
#define ERROR(msg) fprintf(stderr, "ERROR: %s\n", msg)


#endif
