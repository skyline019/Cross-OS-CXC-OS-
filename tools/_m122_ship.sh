#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export GIT_AUTHOR_NAME='VoyagerStar'
export GIT_AUTHOR_EMAIL='voyagerstar@users.noreply.github.com'
export GIT_COMMITTER_NAME='VoyagerStar'
export GIT_COMMITTER_EMAIL='voyagerstar@users.noreply.github.com'
git commit -F tools/_m122_commit_msg.txt
git status
GIT_SSH_COMMAND='ssh -o BatchMode=yes' git push origin HEAD
git log -1 --oneline
git status
