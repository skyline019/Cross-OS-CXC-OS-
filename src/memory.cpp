#include <stddef.h>
#include <stdint.h>

// GCC/MinGW freestanding implementation detail: may_alias permits these
// aligned word accesses without relying on the effective type of the backing
// byte storage. Unaligned addresses always take the byte path.
typedef uint64_t alias_word __attribute__((may_alias));

// Freestanding memory routines required both by kernel code and by compiler
// generated structure copies/zero-initialization.
extern "C" void* memcpy(void* destination, const void* source, size_t size) {
    auto* output = static_cast<uint8_t*>(destination);
    const auto* input = static_cast<const uint8_t*>(source);

    if (((reinterpret_cast<uintptr_t>(output) |
          reinterpret_cast<uintptr_t>(input)) & 7) == 0) {
        auto* output_words = reinterpret_cast<alias_word*>(output);
        const auto* input_words = reinterpret_cast<const alias_word*>(input);
        while (size >= 32) {
            output_words[0] = input_words[0];
            output_words[1] = input_words[1];
            output_words[2] = input_words[2];
            output_words[3] = input_words[3];
            output_words += 4;
            input_words += 4;
            size -= 32;
        }
        while (size >= 8) {
            *output_words++ = *input_words++;
            size -= 8;
        }
        output = reinterpret_cast<uint8_t*>(output_words);
        input = reinterpret_cast<const uint8_t*>(input_words);
    }

    for (size_t index = 0; index < size; ++index) {
        output[index] = input[index];
    }
    return destination;
}

extern "C" void* memmove(void* destination, const void* source, size_t size) {
    auto* output = static_cast<uint8_t*>(destination);
    const auto* input = static_cast<const uint8_t*>(source);
    if (output < input) {
        for (size_t index = 0; index < size; ++index) {
            output[index] = input[index];
        }
    } else if (output > input) {
        for (size_t index = size; index != 0; --index) {
            output[index - 1] = input[index - 1];
        }
    }
    return destination;
}

extern "C" void* memset(void* destination, int value, size_t size) {
    auto* output = static_cast<uint8_t*>(destination);
    const uint8_t byte = static_cast<uint8_t>(value);

    while (size != 0 && (reinterpret_cast<uintptr_t>(output) & 7) != 0) {
        *output++ = byte;
        --size;
    }

    const uint64_t pattern = uint64_t{0x0101010101010101} * byte;
    auto* words = reinterpret_cast<alias_word*>(output);
    while (size >= 32) {
        words[0] = pattern;
        words[1] = pattern;
        words[2] = pattern;
        words[3] = pattern;
        words += 4;
        size -= 32;
    }
    while (size >= 8) {
        *words++ = pattern;
        size -= 8;
    }

    output = reinterpret_cast<uint8_t*>(words);
    for (size_t index = 0; index < size; ++index) {
        output[index] = byte;
    }
    return destination;
}

extern "C" int memcmp(const void* left, const void* right, size_t size) {
    const auto* a = static_cast<const uint8_t*>(left);
    const auto* b = static_cast<const uint8_t*>(right);
    for (size_t index = 0; index < size; ++index) {
        if (a[index] != b[index]) {
            return static_cast<int>(a[index]) - static_cast<int>(b[index]);
        }
    }
    return 0;
}

extern "C" size_t strlen(const char* value) {
    size_t length = 0;
    while (value[length] != '\0') {
        ++length;
    }
    return length;
}
