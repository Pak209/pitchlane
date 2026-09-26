# shellcheck shell=bash
# Shared helpers for the release scripts (sourced, not executed).
set -euo pipefail

err() { echo "::error::$*" >&2; }
note() { echo "::notice::$*"; }
group() { echo "::group::$*"; }
endgroup() { echo "::endgroup::"; }
