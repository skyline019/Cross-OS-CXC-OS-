/* Minimal libc ABI surface for host-gcc link smokes (not musl). */
#pragma once

typedef long ssize_t;
typedef unsigned long size_t;
typedef long intptr_t;
typedef unsigned long uintptr_t;
typedef int mode_t;
typedef int pid_t;

struct iovec {
    void* iov_base;
    size_t iov_len;
};

ssize_t read(int fd, void* buf, size_t count);
ssize_t write(int fd, const void* buf, size_t count);
ssize_t writev(int fd, const struct iovec* iov, int iovcnt);
int open(const char* path, int flags, mode_t mode);
int close(int fd);
void* brk(void* addr);
void* sbrk(intptr_t increment);
long set_tid_address(int* tidptr);
pid_t getpid(void);
pid_t getppid(void);
void _exit(int status) __attribute__((noreturn));
