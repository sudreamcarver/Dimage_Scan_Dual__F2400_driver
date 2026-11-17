#!/bin/sh
# Build and run the native C core with its desktop UI.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cmake -S "$project_dir" -B "$project_dir/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$project_dir/build" -j
exec "$project_dir/run-ui.sh" "$@"
