#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
git add \
  .github/workflows/ci.yml CMakeLists.txt README.md \
  docs/README.zh-CN.md docs/a2-mini-cc.md docs/adr-user-address-space.md \
  docs/midfield-cadence.md \
  include/cxc/user.hpp \
  src/kernel.cpp src/syscall.cpp src/user.cpp \
  tools/cc_host_test.c tools/contract_test.py tools/run_cc_host_test.py \
  user/cxc_cc/as.c user/cxc_cc/cc.h user/cxc_cc/emit.c user/cxc_cc/main.c \
  user/prebuilt/cxc_cc.elf
export GIT_AUTHOR_NAME='VoyagerStar'
export GIT_AUTHOR_EMAIL='voyagerstar@users.noreply.github.com'
export GIT_COMMITTER_NAME='VoyagerStar'
export GIT_COMMITTER_EMAIL='voyagerstar@users.noreply.github.com'
git commit -F tools/_m127_commit_msg.txt
GIT_SSH_COMMAND='ssh -o BatchMode=yes' git push origin HEAD
git log -1 --oneline
git status -sb
