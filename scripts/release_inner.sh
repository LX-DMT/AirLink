#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"$script_dir/doctor.sh" --strict
"$script_dir/build_components.sh"
"$script_dir/build_sdk.sh"
"$script_dir/assemble_release.sh"
"$script_dir/verify_release.sh"
