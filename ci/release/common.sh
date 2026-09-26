# shellcheck shell=bash
# Shared helpers for the release scripts (sourced, not executed).
set -euo pipefail

# PL_PLAIN_ERRORS=1 (self-tests) prints errors without creating GitHub annotations.
err() { if [ -n "${PL_PLAIN_ERRORS:-}" ]; then echo "error (expected in self-test): $*" >&2; else echo "::error::$*" >&2; fi; }
note() { echo "::notice::$*"; }
group() { echo "::group::$*"; }
endgroup() { echo "::endgroup::"; }
