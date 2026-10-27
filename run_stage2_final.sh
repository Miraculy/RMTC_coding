#!/usr/bin/env bash
set -eo pipefail
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
TEAM="${1:-A}"
case "$TEAM" in A|B|C|D) ;; *) echo '席位必须为 A/B/C/D' >&2; exit 2;; esac
if (( $# >= 1 )); then shift; fi
PACKAGE="$HERE/stage2/install/rmtc_bot"
if [[ ! -x "$PACKAGE/lib/rmtc_bot/contestant" ]]; then
  echo '二阶段发布程序尚未构建，请在仓库根执行 ./build_stage2.sh' >&2
  exit 2
fi
source /opt/ros/humble/setup.bash
source "$HERE/stage2/install/local_setup.bash"
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-27}"
export ROS_LOCALHOST_ONLY="${ROS_LOCALHOST_ONLY:-0}"
export RMW_IMPLEMENTATION="${RMW_IMPLEMENTATION:-rmw_fastrtps_cpp}"
if [[ "${RMTC_AI_PROVIDER:-gateway}" == gateway && -z "${RMTC_TEAM_TOKEN:-}" ]]; then
  echo '未设置 RMTC_TEAM_TOKEN；正式任务资格与事件理解需要本场令牌。' >&2
fi
# Use this package's executable directly, even if another ROS overlay is active.
# Later explicit ROS arguments may override the release profile for diagnostics.
exec "$PACKAGE/lib/rmtc_bot/contestant" --ros-args \
  --params-file "$HERE/stage2/config/final.yaml" \
  -p "team:=$TEAM" -p stage:=2 -p max_ticks:=500 "$@"
