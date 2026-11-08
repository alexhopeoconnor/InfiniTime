#!/usr/bin/env bash
set -euo pipefail

test_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$test_dir/../.." && pwd)"
test_build_dir="$(mktemp -d /tmp/elixir-controller-test.XXXXXX)"
trap 'rm -rf -- "$test_build_dir"' EXIT

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
  -I"$test_dir/stubs" -I"$repo_root/src" \
  "$repo_root/src/components/heartrate/HeartRateController.cpp" \
  "$test_dir/controller.cpp" -o "$test_build_dir/controller"
"$test_build_dir/controller"
