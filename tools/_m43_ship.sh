#!/usr/bin/env bash
set -euo pipefail
cd /mnt/e/OS/workspace-study/cxc-os

export GIT_AUTHOR_NAME='skyline019'
export GIT_AUTHOR_EMAIL='skyline019@users.noreply.github.com'
export GIT_COMMITTER_NAME="$GIT_AUTHOR_NAME"
export GIT_COMMITTER_EMAIL="$GIT_AUTHOR_EMAIL"

git add \
  .github/workflows/ci.yml \
  CMakeLists.txt \
  README.md \
  docs/README.zh-CN.md \
  docs/adr-user-address-space.md \
  docs/midfield-cadence.md \
  docs/phase1-assessment-roadmap.md \
  include/cxc/syscall.hpp \
  include/cxc/user.hpp \
  include/cxc/user_munmap_blob.hpp \
  include/cxc/vmm.hpp \
  src/kernel.cpp \
  src/syscall.cpp \
  src/user.cpp \
  src/vmm.cpp \
  tools/contract_test.py \
  tools/make_munmap_elf.py

git status --short
git commit -m "$(cat <<'EOF'
feat: M43 munmap round-trip user probe (v0.44.0)

Add unmap_user_page and Linux munmap so a user ELF can map, touch, and
free an anonymous page, printing USER_MUNMAP_PASS without regressing
M13–M42 contracts.
EOF
)"
git log -1 --oneline
GIT_CONFIG_GLOBAL=/dev/null git push git@github.com:skyline019/Cross-OS-CXC-OS-.git HEAD:main
rm -f tools/_m43_ship.sh tools/_m43_build_test.sh tools/_m42_build_test.sh tools/_m42_ship.sh
git status --short
