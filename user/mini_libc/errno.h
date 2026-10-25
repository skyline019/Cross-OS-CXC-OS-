/* Minimal errno for M57 mini-libc (Linux values; not musl). */
#pragma once

#define EPERM 1
#define ENOENT 2
#define EIO 5
#define ENOMEM 12
#define EFAULT 14
#define EINVAL 22
#define ENOSYS 38

extern int errno;

int* __errno_location(void);
