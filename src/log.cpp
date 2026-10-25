#include <cxc/log.hpp>

#include <cxc/cpu.hpp>

#include <cxc/serial.hpp>

namespace cxc::log {
namespace {

EFI_SYSTEM_TABLE* system_table = nullptr;
bool uefi_console_enabled = false;
bool serial_console_enabled = false;

size_t string_length(const char* value) {
    size_t length = 0;
    while (value[length] != '\0') {
        ++length;
    }
    return length;
}

void uefi_write(const char* value, size_t size) {
    if (!uefi_console_enabled || system_table == nullptr ||
        system_table->con_out == nullptr ||
        system_table->con_out->output_string == nullptr) {
        return;
    }

    // All current kernel messages are ASCII. Convert in bounded chunks so no
    // heap allocation is needed during early boot or panic paths.
    CHAR16 buffer[97];
    size_t used = 0;
    for (size_t index = 0; index < size; ++index) {
        const char ch = value[index];
        if (ch == '\n') {
            if (used + 3 >= 96) {
                buffer[used] = u'\0';
                system_table->con_out->output_string(system_table->con_out, buffer);
                used = 0;
            }
            buffer[used++] = u'\r';
            buffer[used++] = u'\n';
        } else {
            if (used + 2 >= 96) {
                buffer[used] = u'\0';
                system_table->con_out->output_string(system_table->con_out, buffer);
                used = 0;
            }
            buffer[used++] = static_cast<CHAR16>(static_cast<unsigned char>(ch));
        }
    }
    buffer[used] = u'\0';
    if (used != 0) {
        system_table->con_out->output_string(system_table->con_out, buffer);
    }
}

}  // namespace

void initialize(EFI_SYSTEM_TABLE* table) {
    system_table = table;
    uefi_console_enabled = table != nullptr && table->con_out != nullptr;
    serial_console_enabled = false;
    serial::initialize();
}

void disable_uefi_console() {
    uefi_console_enabled = false;
    system_table = nullptr;
    // OVMF routes ConOut to the same UART. Switching rather than duplicating
    // sinks keeps one clean copy of every message in the serial log.
    serial_console_enabled = true;
}

void write(char value) {
    if (serial_console_enabled) {
        serial::write(value);
    }
    uefi_write(&value, 1);
}

void write(const char* value, size_t size) {
    if (serial_console_enabled) {
        for (size_t index = 0; index < size; ++index) {
            serial::write(value[index]);
        }
    }
    uefi_write(value, size);
}

void write(const char* value) {
    if (value == nullptr) {
        write("(null)", 6);
        return;
    }
    write(value, string_length(value));
}

void line(const char* value) {
    write(value);
    write('\n');
}

void decimal(uint64_t value) {
    char buffer[20];
    size_t used = 0;
    do {
        buffer[used++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (used != 0) {
        write(buffer[--used]);
    }
}

void hex(uint64_t value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    write("0x", 2);
    for (int shift = 60; shift >= 0; shift -= 4) {
        write(digits[(value >> shift) & 0x0f]);
    }
}

[[noreturn]] void panic(const char* message) {
    write("\nCXC_OS_PANIC: ");
    write(message);
    write('\n');
    cpu::halt();
}

}  // namespace cxc::log
