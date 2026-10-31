# 二阶段 v2.1（guard+F1）并入正式包记录

日期：2026-10-29。按用户要求，将本地 guard+F1 候选并入正式二阶段包；正式包此前为 v2（dcd77d8）。

## 变更
在 v2 基础上启用两组已实现但默认关闭的参数（代码来自 experiments/stage2-opportunity-20261028 的 candidate，已去除 F3/blockmem/opening 等实验项）：
- `observation_detour`：已确定目标附近额外 1–2 步的有界观察改道；
- `resource_arrival_value`：按到达时间估计资源可获得性，替换而非叠加竞争折扣。

正式 `stage2/config/final.yaml` 新增参数：
- observation_detour: true
- observation_extra_limit: 2
- observation_attempt_limit: 1
- observation_min_tick: 8
- observation_max_known_resources: 1
- resource_arrival_value: true

回滚方式：将上述参数改回 false/默认值即可退回 v2 行为。

## 修正评测结果（AI 关闭、mock、冻结对手）
使用修正版 tools/evaluate.py：加载 final.yaml、固定端口/domain、按 session 校验 0–499 决策、receipt、final、board；只统计完整有效局。guard+F1 与 v2 同图同席位配对。

### B1（12 图×4，配对 34 局）
| 指标 | v2 | guard+F1 |
| --- | ---: | ---: |
| R | 50.88 | **51.76** |
| 均分 | 40.88 | **41.66** |
| 平均名次 | 2.059 | **1.853** |
| 前二 | 23 | **26** |
| 严格第一 | 15 | 15 |
| 死亡（D 负跳变近似） | 8.24 | 9.06 |
| 轨迹改变 | — | 12/34 |

### B2（8 图×4，32 局）
| 指标 | v2 | guard+F1 |
| --- | ---: | ---: |
| R | 49.38 | 49.38 |
| 均分 | 40.99 | **41.14** |
| 平均名次 | 2.125 | **2.062** |
| 前二 | 20 | **21** |
| 严格第一 | 12 | 12 |
| 死亡 | 8.81 | 8.47 |
| 轨迹改变 | — | 9/32 |

结论：相对 v2，名次/前二/均分改善，R 在 B1 提高约 0.88、B2 持平；B1 死亡略升约 0.8。

## 已知边界
- 未达到 R ≥ base+2 的验收目标（B1 51.76 vs old-base 50.50；B2 49.38 vs old-base 49.38）；
- B1 死亡略差，需更多重复样本；
- 全部数据为 AI 关闭、mock 裁判、冻结对手；真实 DeepSeek 条件尚未验证（2026-10-29 尝试因外网请求超时/429 未形成有效结论）；
- 正式比赛条件仍需赛前联调。

## 验证
- ./build_stage2.sh 通过；
- core_test 136/136、ai_test 14/14 通过；
- tools/verify_stage2_release.py：12 文件、50 项参数，valid；
- 二进制 SHA-256：d89cd81bc43dcf13544c84371631ba36a99c5346c027eed2d2641763891bed7b。
