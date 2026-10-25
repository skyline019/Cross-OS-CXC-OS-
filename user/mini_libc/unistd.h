/* Minimal libc ABI surface for host-gcc link smokes (not musl). */
#pragma once

typedef long ssize_t;
typedef unsigned long size_t;
typedef int mode_t;

ssize_t read(int fd, void* buf, size_t count);
ssize_t write(int fd, const void* buf, size_t count);
int open(const char* path, int flags, mode_t mode);
int close(int fd);
void _exit(int status) __attribute__((noreturn));
