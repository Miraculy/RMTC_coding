#!/usr/bin/env bash
set -eo pipefail
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source /opt/ros/humble/setup.bash
cd "$HERE"
# Keep the released stage-two package independent of the stage-one workspace.
colcon --log-base "$HERE/stage2/log" build \
  --base-paths "$HERE/stage2/src" \
  --build-base "$HERE/stage2/build" --install-base "$HERE/stage2/install" \
  --packages-select rmtc_bot \
  --cmake-args -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "$@"
