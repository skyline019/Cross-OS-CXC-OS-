# musl subset (CXC/OS)

API-shaped **static musl subset** for A2 spine evidence — distinct from `user/mini_libc/` stubs.

- Transport: Linux x86-64 syscall **numbers** via the `syscall` instruction (M66 LSTAR).
- `user/mini_libc/` remains on `int $0x80` as a regression gate.
- Full musl CRT still blocked on auxv / `exit_group` (see `docs/musl-subset-link-notes.md`).
