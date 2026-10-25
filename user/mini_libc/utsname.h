/* Minimal utsname for M61 mini-libc (Linux field widths; not musl). */
#pragma once

enum { _UTSNAME_LENGTH = 65 };

struct utsname {
    char sysname[_UTSNAME_LENGTH];
    char nodename[_UTSNAME_LENGTH];
    char release[_UTSNAME_LENGTH];
    char version[_UTSNAME_LENGTH];
    char machine[_UTSNAME_LENGTH];
    char domainname[_UTSNAME_LENGTH];
};

int uname(struct utsname* buf);
