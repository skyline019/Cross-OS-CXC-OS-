#pragma once

#include <stddef.h>
#include <stdint.h>

#include <cxc/uefi.hpp>

namespace cxc::log {

void initialize(EFI_SYSTEM_TABLE* system_table);
void disable_uefi_console();

void write(char value);
void write(const char* value);
void write(const char* value, size_t size);
void line(const char* value);
void decimal(uint64_t value);
void hex(uint64_t value);

[[noreturn]] void panic(const char* message);

}  // namespace cxc::log
