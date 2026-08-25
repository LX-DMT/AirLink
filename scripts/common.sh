#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C
export TZ=UTC
export PYTHONDONTWRITEBYTECODE=1

repo_root()
{
    cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd
}

die()
{
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

need()
{
    command -v "$1" >/dev/null 2>&1 || die "missing command: $1"
}

sha256()
{
    sha256sum "$1" | awk '{print $1}'
}

load_versions()
{
    local root
    root="$(repo_root)"
    set -a
    # shellcheck disable=SC1091
    source "$root/versions.lock"
    set +a
    for name in AIRLINK_PRODUCT AIRLINK_VERSION AIRLINK_BUILD AIRLINK_TAG \
        AIRLINK_IMAGE C906L_FIRMWARE LINUX_FIRMWARE IPC_PROTOCOL IPC_ABI; do
        printenv "$name" >/dev/null || die "versions.lock is missing $name"
    done
}
