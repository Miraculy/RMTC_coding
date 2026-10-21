#!/usr/bin/env bash
set -eo pipefail
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
TEAM="${1:-A}"
STAGE="${2:-2}"
case "$TEAM" in A|B|C|D) ;; *) echo '席位必须为 A/B/C/D' >&2; exit 2;; esac
case "$STAGE" in 1) MAX_TICKS=100;; 2) MAX_TICKS=500;; *) echo '阶段必须为 1/2' >&2; exit 2;; esac
if (( $# >= 1 )); then shift; fi
if (( $# >= 1 )); then shift; fi
source /opt/ros/humble/setup.bash
source "$HERE/install/setup.bash"
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-27}"
export ROS_LOCALHOST_ONLY="${ROS_LOCALHOST_ONLY:-0}"
export RMW_IMPLEMENTATION="${RMW_IMPLEMENTATION:-rmw_fastrtps_cpp}"
if [[ "$STAGE" == 2 && "${RMTC_AI_PROVIDER:-gateway}" == gateway && -z "${RMTC_TEAM_TOKEN:-}" ]]; then
  echo '未设置 RMTC_TEAM_TOKEN；可移动和战斗，但正式任务资格与事件理解需要本场令牌。' >&2
fi
exec ros2 run rmtc_bot contestant --ros-args \
  -p "team:=$TEAM" -p "stage:=$STAGE" -p "max_ticks:=$MAX_TICKS" "$@"
