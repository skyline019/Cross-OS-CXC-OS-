/* Minimal string for M58 mini-libc (not musl). */
#pragma once

#include "mini_libc/unistd.h"

void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* s, int c, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);
size_t strlen(const char* s);
int strcmp(const char* s1, const char* s2);
