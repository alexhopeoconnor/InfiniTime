#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
test_build_dir="$(mktemp -d "${TMPDIR:-/tmp}/elixir-ppg-test.XXXXXX")"
trap 'rm -rf "$test_build_dir"' EXIT

# Ppg.cpp includes the Nordic logging header but does not call any log API.
printf '#pragma once\n' > "$test_build_dir/nrf_log.h"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra \
  -I"$test_build_dir" -I"$repo_root/src" \
  "$repo_root/tests/PpgRegression.cpp" \
  "$repo_root/src/components/heartrate/Ppg.cpp" \
  -o "$test_build_dir/ppg-regression"
"$test_build_dir/ppg-regression"
