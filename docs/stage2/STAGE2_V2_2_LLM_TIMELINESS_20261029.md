# 二阶段 v2.2：LLM 时效补丁

日期：2026-10-29。用户要求在 v2.1（guard+F1）基础上合入已验证的 LLM 时效优化并推送。

## 改动
只改 `stage2/src/rmtc_bot/src/node.cpp`：
- `/referee/event_text` 话题回调只提交含“预告”的公告；
- perception 的 event_text 循环同样只提交含“预告”的行；
- 开始/命中/结束/效果获得/解除等派生行不再触发独立模型请求；
- AI 客户端原有字节级去重、schema 修复、超时/配额保护均不变。

规划器、`final.yaml`、无 AI 行为均未改动。

## 验证
- `./build_stage2.sh` 通过；`core_test 136/136`、`ai_test 14/14` 通过；
- S03 24×24 真实 DeepSeek：AI 请求从 50 降到 18；成功/接受/应用 18/17/17；八类事件首次进入记忆提前 1–4 Tick；A 最终 45.0 第一，与 AI-off 有 383/500 决策不同；
- S04 同图 A/B：AI-on 17/17 成功/应用，事件提前 2–4 Tick；A 与 AI-off 394/500 决策不同；两局 A 得分、名次、R、M 相同（S04 42.5 第一，R=50，M=3），D AI-on 520 vs AI-off 720；
- 结论：补丁解决“解释晚到、请求排队”的时效问题，但不宣称得分收益；LLM 当前稳定的行为影响主要是堡垒目标覆盖，需后续约束/评估。

## 回滚
回退 `node.cpp` 中两处 `notice.find("预告")` 过滤即可恢复 v2.1 行为。

## 构建产物
- 二进制 SHA-256：`16bb58ccdc16f7b338eefaf18cb22b7c4ea80e0a1aaabfe4d15970d3d0d66503`
- 源码提交：本次提交
