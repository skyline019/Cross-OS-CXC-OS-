#include "mini_libc/errno.h"

int errno;

int* __errno_location(void) {
    return &errno;
}
