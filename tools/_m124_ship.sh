#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
git add \
  .github/workflows/ci.yml CMakeLists.txt README.md \
  docs/README.zh-CN.md docs/a2-mini-cc.md docs/adr-user-address-space.md \
  docs/midfield-cadence.md docs/midpoint-assessment-11.md \
  docs/midpoint-assessment-12.md \
  include/cxc/user.hpp \
  include/cxc/musl_strncmp_elf.hpp include/cxc/musl_strcpy_elf.hpp \
  include/cxc/musl_stpcpy_elf.hpp \
  src/kernel.cpp src/syscall.cpp src/user.cpp \
  tools/cc_host_test.c tools/contract_test.py tools/make_musl_mem_elf.py \
  tools/run_cc_host_test.py \
  user/cxc_cc/emit.c \
  user/prebuilt/cxc_cc.elf \
  user/prebuilt/musl_strncmp.o user/prebuilt/musl_strcpy.o \
  user/prebuilt/musl_stpcpy.o
export GIT_AUTHOR_NAME='VoyagerStar'
export GIT_AUTHOR_EMAIL='voyagerstar@users.noreply.github.com'
export GIT_COMMITTER_NAME='VoyagerStar'
export GIT_COMMITTER_EMAIL='voyagerstar@users.noreply.github.com'
git commit -F tools/_m124_commit_msg.txt
GIT_SSH_COMMAND='ssh -o BatchMode=yes' git push origin HEAD
git log -1 --oneline
git status -sb
