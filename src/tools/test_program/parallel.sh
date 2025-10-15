#!/bin/bash
set -euo pipefail


# directory of this script
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# expected built binary (relative from this folder)
candidate="$script_dir/../../../../../build/src/ICUB/bin/test_program"


if [ -z "$candidate" ] || [ ! -x "$candidate" ]; then
  echo "Error: test_program not found or not executable."
  echo "Expected at: $script_dir/../../../../../build/src/ICUB/bin/test_program"
  exit 1
fi

# run from the script directory so logs created by the program end up here
cd "$script_dir"

echo "Running: $candidate parallel_program (logs will be written to $(pwd))"
"$candidate" parallel_program