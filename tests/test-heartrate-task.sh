#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
  -I "$repo_root/tests/heartrate-task/stubs" -I "$repo_root/src" \
  "$repo_root/tests/heartrate-task/HeartRateTaskRegression.cpp" \
  "$repo_root/src/heartratetask/HeartRateTask.cpp" \
  -o "$test_dir/heartrate-task"
"$test_dir/heartrate-task"
