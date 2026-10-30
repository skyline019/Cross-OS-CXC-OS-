"""Host tests for exact fail-closed NVMe boot markers under SMP UART races."""
import random
import unittest

from nvme_boot_tests import expected_panic


class PanicMarkerTests(unittest.TestCase):
    def test_smp_uart_prefix_and_complete_message(self):
        message = b"log: invalid header count"
        self.assertTrue(expected_panic(b"hart 2 starpanic: " + message + b"\r\n", message, 3))
        self.assertTrue(expected_panic(b"panic: " + message + b"\n", message, 3))
        self.assertFalse(expected_panic(b"panic: " + message, message, 3))
        self.assertFalse(expected_panic(b"panic: " + message + b" extra\r\n", message, 3))
        self.assertFalse(expected_panic(b"panic: log: invalid header destination\r\n", message, 3))

    def test_interleaving_inside_panic(self):
        message = b"log: invalid header destination"
        target = b"panic: " + message + b"\n"
        self.assertTrue(expected_panic(b"phanic: " + message + b"\r\n", message, 3))
        # Interleave every byte of an in-progress hart-starting message, not
        # just one extra prefix or one contiguous fragment.
        noise = b"hart 2 starting\n"
        mixed = bytearray()
        for i, char in enumerate(target):
            mixed.append(char)
            if i < len(noise):
                mixed.append(noise[i])
        self.assertTrue(expected_panic(mixed, message, 3))
        self.assertFalse(expected_panic(b"pXanic: " + message + b"\n", message, 3))
        self.assertFalse(expected_panic(b"phanic: " + message + b"\n", message, 1))

    def test_many_boot_line_slices_and_interleavings(self):
        rng = random.Random(20261030)
        message = b"log: invalid header destination"
        target = b"panic: " + message + b"\n"
        for _ in range(200):
            hart = rng.choice((1, 2))
            full = f"hart {hart} starting\n".encode()
            begin = rng.randrange(len(full))
            end = rng.randrange(begin, len(full) + 1)
            fragment = full[begin:end]
            i = j = 0
            mixed = bytearray()
            while i < len(target) or j < len(fragment):
                take_noise = j < len(fragment) and (i == len(target) or rng.randrange(2))
                if take_noise:
                    mixed.append(fragment[j])
                    j += 1
                else:
                    mixed.append(target[i])
                    i += 1
            self.assertTrue(expected_panic(mixed, message, 3), bytes(mixed))

    def test_message_is_literal_not_regex(self):
        message = b"failure (unexpected)"
        self.assertTrue(expected_panic(b"panic: failure (unexpected)\n", message, 1))
        self.assertFalse(expected_panic(b"panic: failure unexpected\n", message, 1))


if __name__ == "__main__":
    unittest.main()
