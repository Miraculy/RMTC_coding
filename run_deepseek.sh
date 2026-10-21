#!/usr/bin/env bash
# Local practice with the official DeepSeek API; never embeds credentials.
set -eo pipefail
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
export RMTC_AI_PROVIDER=deepseek
export AI_GATEWAY=https://api.deepseek.com/chat/completions
export RMTC_AI_MODEL="${RMTC_AI_MODEL:-deepseek-flash}"
if [[ -z "${DEEPSEEK_API_KEY:-}" ]]; then
  read -r -s -p 'DeepSeek API key: ' DEEPSEEK_API_KEY
  echo
  export DEEPSEEK_API_KEY
fi
if [[ -z "$DEEPSEEK_API_KEY" ]]; then
  echo 'DeepSeek API key 不能为空' >&2
  exit 2
fi
exec "$HERE/run_contestant.sh" "${1:-A}" "${2:-2}" \
  "${@:3}" -p ai_provider:=deepseek -p "ai_gateway:=$AI_GATEWAY" -p "ai_model:=$RMTC_AI_MODEL"
