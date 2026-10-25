/* Minimal stdlib for M56 bump malloc smoke (not musl). */
#pragma once

#include "mini_libc/unistd.h"

void* malloc(size_t size);
void free(void* ptr);
