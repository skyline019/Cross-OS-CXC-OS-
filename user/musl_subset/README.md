# musl subset (CXC/OS)

API-shaped **static musl subset** for A2 spine evidence — distinct from `user/mini_libc/` stubs.

- Transport today: Linux x86-64 syscall **numbers** via `int $0x80` (CXC kernel gate).
- Upstream musl 1.2.4 on x86_64 uses the `syscall` instruction; that path is blocked until LSTAR (see `docs/musl-subset-link-notes.md`).
- This directory is the labeled-equivalent subset that can already regress under the current kernel contract.
