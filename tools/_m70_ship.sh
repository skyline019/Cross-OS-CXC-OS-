#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
git add \
  .github/workflows/ci.yml \
  CMakeLists.txt \
  README.md \
  docs/README.zh-CN.md \
  docs/adr-user-address-space.md \
  docs/midfield-cadence.md \
  docs/musl-subset-link-notes.md \
  docs/phase1-assessment-roadmap.md \
  include/cxc/syscall.hpp \
  include/cxc/user.hpp \
  src/kernel.cpp \
  src/syscall.cpp \
  src/user.cpp \
  tools/contract_test.py \
  user/cross_hello.ld \
  user/musl_printf_probe.c \
  user/prebuilt/musl_printf.elf
git commit -F tools/_m70_commit_msg.txt
git status -sb
git push origin HEAD
git log -1 --oneline
git rev-parse HEAD
