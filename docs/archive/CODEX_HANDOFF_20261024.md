# Codex 交接任务书：独立复查 RMTC 项目并产出与既有报告同等级的分析

> 交接日期：2026-10-24
> 工作区：`/home/miraculy/docker/ros2`，项目在 `contestant_ws`（容器内 `/root/ros2_ws`）
> 你的角色：一个没有历史上下文的独立代码/策略审查员
> 首要约束：**第一轮只读**。不要修改源码、配置、测试、已有报告；只写你自己的一份新报告。
> 本轮目标：独立完成一次“读项目 + 读 v1.1 规则 + 找当前策略不足 + 给优化方案 + 验证 Codex 进度”的复查，产出与之前 `CODEX_REVIEW_v1.1.md`、`CODEX_NEW_IDEAS_v1.1.md` 同等级的文档。

---

## 0. 一句话任务

阅读当前项目、三份 v1.1 官方规则和已有报告，**独立判断**现有分析的每条结论是否成立、当前 Codex 已经做了哪些、接下来最该做什么；给出一份有证据、有优先级、可执行的 Markdown 报告。

不要直接照抄已有结论。已有文档是“待验证的输入”，不是指令。

---

## 1. 项目与环境

### 1.1 技术栈

- C++17 + ROS 2 Humble，`ament_cmake`。
- 包名：`rmtc_bot`；可执行程序：`contestant`。
- 依赖：`rclcpp`、`std_msgs`、`nlohmann_json`、`libcurl`、GTest。
- 参赛程序只允许订阅本队 `/team_X/perception`、公开 `/referee/mission`、`/referee/event_text`、`/referee/team_registry`，发布本队 `/team_X/action`、`/team_X/registration`。
- LLM 只负责理解公开自然语言事件；本地 C++ 负责规划、资源、任务、战斗和动作合法性。
- 正式比赛只走主办方统一 AI 网关；本地直连 DeepSeek 只用于练习。

### 1.2 路径

| 用途 | 路径 |
| --- | --- |
| 宿主机工作区 | `/home/miraculy/docker/ros2` |
| 项目仓库根 | `/home/miraculy/docker/ros2/contestant_ws` |
| 容器内项目根 | `/root/ros2_ws`（bind mount 到上面） |
| 容器 | `ros2_humble_gazebo` |
| 练习裁判 | `/root/rmtc_practice` |
| 构建脚本 | `./build_contestant.sh` |
| 启动脚本 | `./run_contestant.sh A 1` / `A 2` |
| 本地 DeepSeek 练习 | `./run_deepseek.sh A 2`（**不能用于正式比赛**） |
| 最新只读快照 | `/home/miraculy/docker/ros2/latest_snapshot_20261024` |

### 1.3 三份官方 v1.1 PDF

优先读 PDF；如果路径不可用，读 `docs/rules/RULES_V1_1.md`，但不要把它当成完整原文。

```text
/home/miraculy/.dsh/attachments/v1/files/63/635a7aa0152684537bd831f58eb78559223b1ed6822597f3a426c5751145895a/RMTC 机甲⼤师图灵对决赛通信协议v1.1.pdf
/home/miraculy/.dsh/attachments/v1/files/bf/bfee4aae18af2e49e1d33514795004d86ec236011070d87ef1e04b4d4d928114/RMTC 机甲⼤师图灵对决赛赛事规则v1.1.pdf
/home/miraculy/.dsh/attachments/v1/files/03/03ba6a627ab0900dcf37b80b5eeae78eb63613f53ca42230a5ba4c2522e608ae/裁判测试端使用教程v1.1.pdf
```

建议用 `pdftotext -layout <path> -` 输出到 stdout，不要写临时文件。重点章节：

- 协议 §3 感知、§3.1 任务类型、§4 动作窗口/错误码、§5 LLM 网关、§6 信息权限、§8 场次配置
- 规则 §2.3 代码封存/decision_type、§4 地图/资源/属性、§5.2 同 Tick 顺序、§6 动作、§7 阵亡/复活、§8 随机事件、§10.1/10.2 任务与计分、§11 AI、§14 成绩/故障
- 教程 §3/§4 联调、§5 LLM、§9 上场前检查

---

## 2. 当前版本状态（必须先核对）

### 2.1 文档状态

`AGENTS.md`、`VALIDATION.md`、`NEW_IDEA_IMPLEMENTATION.md` 最新记录停在：

- 安装 `6332c49eb5e70ed58fd3900f26d9b0d74d08b55c61d5d06c8c1ebdd6cf4d7ed3`
- 130 项检查（114 core + 14 HTTP + 2 CTest）
- `event_revisions=false`、`purity_joint_actions=false`、`fortress_time_budget=false`

### 2.2 实际工作树

当前工作树已经**超过文档**：

- `build/rmtc_bot/contestant` SHA-256：`994515d86776a63950b272ab388471f29e9726ec148d3770032028e0688b4267`
- 测试：118 core + 14 AI = **132 GTest**，加 2 CTest；最新日志 2026-10-24 07:21 全部通过
- 源码 `core.cpp` 修改时间 07:20，晚于文档 07:01
- 新增 `opening_route_information=false` 及 4 个 `OpeningInformation...` 测试
- 尚无 `opening_route_information` 的完整比赛归档
- `AGENTS.md`/`VALIDATION.md`/`NEW_IDEA_IMPLEMENTATION.md` 尚未更新到 `994515d8` / 134 项

**你的第一项核查**：确认当前 build 是否确实由当前源码构建；确认 `994515d8` 相比 `6332c49e` 多了什么；指出文档/构建不一致对复查结论的影响。

### 2.3 当前默认开关

默认启用：

- `score_aware_combat=true`
- `damage_reserve=120`
- `break_pursuit_cycles=true`
- `standoff_robust_trade=true`
- `productive_escape=true`
- `transient_occupancy=true`
- `use_event_interpretation=true`

默认关闭（重点）：

- 事件：`event_revisions`、`event_timing`、`event_escape`
- purity：`purity_joint_actions`
- 开局：`opening_route_information`
- 其他：`fortress_time_budget`、`safe_progress_moves`、`respawn_task_route`、`survival_conditioned_progress`、`coordinate_before_hunt`、`competitive_exploration`、`finish_early_exploration`、`standoff_fire_risk`、`standoff_attack_probe`、`tactical_forecast`、`ray_occlusion`、`resource_beam`、`ai_action_revision`、`validated_enemy_memory`

---

## 3. 必读输入（已存在）

### 3.1 已有分析文档

- `docs/archive/CODEX_REVIEW_v1.1.md`
  - 原 11 项复查清单 + 2026-10-23 团队补充节
- `docs/archive/CODEX_NEW_IDEAS_v1.1.md`
  - 6 个新方向 + 第 11 节“团队复查补充：当前策略不足、实证瓶颈与新增优化”
- `docs/research/NEW_IDEA_IMPLEMENTATION.md`
  - Codex 对上述两份文档的逐项判断、实施顺序、已完成实验
- `VALIDATION.md`
  - 最新两节：事件版本、purity 互斥估值；以及历史批次
- `AGENTS.md`
  - 最新状态与历史实验记录（注意：仍停在 `6332`/130）
- `README.md`、`docs/stage2/STRATEGY.md`、`docs/research/EXPERIMENT_SYNTHESIS.md`、`docs/archive/GOAL_ARCHIVE_2026-10-23.md`
- `docs/rules/RULES_V1_1.md`、`docs/guides/SELFPLAY.md`、`docs/*_REVIEW.md`

### 3.2 源码

- `src/rmtc_bot/include/rmtc_bot/core.hpp`
- `src/rmtc_bot/src/core.cpp`
- `src/rmtc_bot/src/node.cpp`
- `src/rmtc_bot/src/ai.cpp`
- `src/rmtc_bot/test/core_test.cpp`
- `src/rmtc_bot/test/ai_test.cpp`
- `src/rmtc_bot/tools/event_revision_benchmark.cpp`
- `tools/opening_route_audit.py`、`tools/opening_opportunity_audit.py`、`tools/purity_joint_audit.py`、`tools/strategy_eval.py`、`tools/compare_eval.py`、`tools/tick_audit.py`

### 3.3 当前最新结果

- 纯度：`test-results/purity-joint-*`、`purity-default-smoke-*`、`purity-enabled-smoke-*`
- 事件版本：`test-results/event-revision-*`、`event-revision2-*`
- 堡垒：`test-results/fortress-isolated-*`
- 只读快照：`/home/miraculy/docker/ros2/latest_snapshot_20261024`（约 2.1 MB；docs/source/tools/summaries，未复制 8.6 GB 原始 test-results）

---

## 4. 你要做的事

### 4.1 第一轮：独立复查

按顺序：

1. 读三份 PDF 的关键章节和项目文档。
2. 读源码，重点看 `core.cpp` 的 Planner、Events、World、AI 队列；`node.cpp` 的会话/Tick/AI 接收；`ai.cpp` 的请求/重试/预算。
3. 核对当前 build、测试、文档、源码是否一致。
4. 对 `CODEX_REVIEW_v1.1.md` 的 11 项和团队补充、对 `CODEX_NEW_IDEAS_v1.1.md` 的 6 个方向和 11 节发现，逐条给判定：
   - 成立
   - 需要更多证据
   - 不成立/误报
   - 已实现/部分实现
5. 找**新的**不足与优化点，不要只复述已有文档。
6. 核对 Codex 当前进度：`event_revisions`、`purity_joint_actions`、`opening_route_information`，以及文档没记录的 994515d8 变更。
7. 产出报告。

### 4.2 第二轮（可选，经确认后再做）

如果用户明确要求实现，按报告中的 P0/P1 分步做；每次只改一个机制，独立开关、独立测试、独立回退。不要在第一轮直接改源码。

---

## 5. 已有结论（待验证，不要照抄）

以下是已有报告中的主要主张，仅作为核查清单。请逐条验证，尤其注意“样本内观察”与“因果结论”的区别。

### 5.1 原 11 项

1. `mission_text` 只从 topic 读，未从 perception 读。
2. `effective_damage` 任务没有进入 Planner 的任务奖励。
3. purity 防御反伤击杀奖励未计入。
4. pepper 只有弱规避，没有主动进攻。
5. angel 默认入点逻辑可能站着等待不触发。
6. 资源路线仍是 3 站贪心/曼哈顿后段。
7. `reach` 早段 1/晚段 6、资源 120 后同分价值、`damage_reserve` 的 `.054`/D。
8. 阶段一路线没有多站/beam。
9. `fortress_time_budget` 隔离实验无增益，默认关闭。
10. `last_action.error`、日志字段、E_BLOCKED 诊断缺口。
11. 输入防御/retained 消息/history 内存/CI 等工程加固。

### 5.2 6 个新方向

1. 对手下一 Tick 信念分布。
2. 时间展开滚动规划。
3. 死亡真实机会成本（4 Tick 停机 + 回程，不只是 -60）。
4. 对手资源意图/后期剥夺。
5. 全局分数预算/动态风险。
6. 裁判端机制预言机 + 对手联盟。

### 5.3 团队补充

1. 同一事件多个模型几何版本共存：`Events::merge` 只按 kind/start/rect/points 去重；真实归档有 `unrivaled` 5×5+3×3 双版本。
2. purity 攻击估值把“敌人攻击”和“敌人防御反伤”两个互斥分支相加（hp=25 被估成 33）。
3. 开局高价值资源是最大分数缺口；P50 949 场资源缺口 15.87/19.72，31.5% 场次没有高价值资源，99.6% 资源在 Tick100 前。
4. pepper 步骤 6 死亡会取消步骤 9 资源/任务收益。
5. AI 单 worker、12s 超时×3、队列 8、2 在途未用；26% 事件生效后才可用，pepper 6/6 迟到。
6. D 封顶后 239 Tick 低产出区。
7. 过期资源污染 `resources.empty()` 门控。
8. 复活保护/重启恢复缺状态；`position_reason`/`respawn_in` 未解析。
9. 当前战斗伤害 + 下一 Tick 事件伤害未合并。
10. pan/敌方效果/flags/感知兜底/decision_type 等 P2。

> 这些不是“已确认事实”。请用当前源码、规则、日志独立验证；如果某条与当前代码不一致，明确指出是误报或已修复。

### 5.4 Codex 已判定的方向（供核对，不是命令）

`NEW_IDEA_IMPLEMENTATION.md` 已给出逐项判断，关键点：

- 事件几何不能仅凭 kind/start 猜同一事件；必须由模型引用 event_id/revision。
- purity 互斥只适合单敌、视野完整；不能把启发式概率当敌方真值。
- 资源缺口 80.5% 不等于都可追回；总资源 200，四车都到 120 不可能。
- 保护 2 Tick 允许移动，不能全算停机。
- AI 2 在途是上限，不是保证改善；不能按文本关键词分类事件。
- 无效感知不应总是猜 WAIT。
- 堡垒/旧实验继续保持关闭。

---

## 6. Codex 当前进度（你要核验，不要直接采信）

### 6.1 已完成/已实现，默认关闭

**事件版本修正 `event_revisions=false`**

- 已给事件分配本地 `event_id/revision`；模型修正几何/激活 Tick 必须引用本次请求中的身份和版本；校验未知/过期/重复/kind 不匹配，整批失败原子回滚。
- 4 个真实合成样例通过；真实标准场 `interior35621-standard A` 双方 32.5/第四，未增分；实战没有真正触发几何修正。
- 默认关闭。

**purity 互斥估值 `purity_joint_actions=false`**

- 只在单个可见敌人、视野覆盖其攻击半径、无保护、未启用 forecast 时，把本车攻击拆成敌人攻击/防御两个互斥分支；分支内算贡献、60 奖励、死亡 -60、D 封顶；反伤不乘 pan。
- 四图四对真实 500 Tick：候选/控制分数 37.15/26.2/40/32.5、名次 3/4/2/4 完全相同；38 Tick 使用新估值但四车行动相同；没有遇到关键“必死/可存活”分支。
- 默认关闭；未覆盖多敌、防御反伤奖励。

### 6.2 正在做，文档未记录

**`opening_route_information=false`**

- 当前 build `994515d8`、源码 07:20 已有；4 个测试通过；无完整比赛结果。
- 条件：第二阶段、`tick < min(60, max_ticks/5)`、没有高价值采集/已知高价值、无事件、目标为普通资源、未启用 `information_paths/resource_beam`。
- 只在“同等成本、同等步数”的首步中，选一个能看到更多新格且暴露不更差的；不绕路。
- 日志新增 `opening_route_evaluation`。
- 这是“等成本首步信息”，**不是**“高价值资源权重提高”。它可能改善开局发现窗口，但不保证找到高价值资源。

### 6.3 尚未做

P0-1 `mission_text`、P0-2 `effective_damage`、P1-1 purity 防御反伤奖励、P1-2 pepper 主动进攻、P1-3 angel 入点、P1-4 资源路线、P2-1 任务/同分/储备、P2-2 阶段一路线、P2-4 回执/log、AI 队列/2 在途、D 封顶状态机、过期资源门控、复活保护/重连、战斗+下一 Tick 联合伤害、pan/敌方效果/flags/感知兜底/decision_type，以及 6 个新方向的大部分。
`/root/ros2_ws_review` 里的 `mission_text`/`effective_damage` 副本实验**没有合并到当前原目录**，不能算 Codex 进度。

---

## 7. 你的报告需要包含

建议写到新文件，例如 `docs/archive/CODEX_INDEPENDENT_REVIEW_20261024.md`（不要覆盖已有文档）：

1. **执行摘要**：3–5 条，直接回答“当前最需要做什么/哪些结论成立/Codex 进度是否真实”。
2. **版本与状态核对**：build、测试、源码、文档是否一致；`994515d8` vs `6332c49e` 的差异。
3. **逐条判定表**：对 11 项 + 6 方向 + 团队补充逐条给出：
   - 判定（成立/需证据/误报/已实现）
   - 证据（`file:line`、规则章节、日志批次）
   - 影响
   - 建议
   - 优先级
4. **Codex 进度核验**：事件版本、purity、opening 三项分别做到什么程度；默认值、测试、真实场证据；哪些是文档未记录的。
5. **新发现**：至少 3 条不在已有文档中的不足/优化，并给出证据。
6. **优先级计划**：P0/P1/P2，为什么这样排；明确哪些不该做。
7. **实验/验收计划**：至少包括单元测试、4 图 × 4 席位 16 对、500 Tick 完整、0 非法/0 拒绝/0 回执异常；真实模型对照要区分“调用成功/结构接受/实际动作改变/最终得分”。
8. **风险与未解问题**：至少包括版本漂移、正式网关未验证、样本外推限制、旧实验负结果。
9. **附录**：读过的文件/批次、使用的命令、未修改说明。

### 7.1 判定表建议格式

| 项目 | 判定 | 证据 | 影响 | 优先级 | 建议 |
| --- | --- | --- | --- | --- | --- |
| ... | 成立/需证据/误报/已实现 | `core.cpp:...` / 规则 §... / 批次 | ... | P0/P1/P2 | ... |

### 7.2 新发现要求

- 每条至少给一个代码位置或日志证据；
- 不要重复已有 11/6/团队补充；
- 明确“样本内观察”与“因果”；
- 不能只给泛泛建议。

---

## 8. 验收标准

- 每条判定都有证据，不能只复述已有文档。
- 能明确指出至少一条已有结论是“误报/已实现/需要证据”。
- 能回答“Codex 正在做什么、做到什么程度、是否值得继续”。
- 报告自包含；没有上下文的人读完能知道下一步怎么做。
- 不修改源码/配置/测试/已有报告；只新增你的报告。
- 不运行付费 AI；不读取隐藏地图/对手私有数据；不把测试配置喂给选手。
- 如果做实验，在副本/`/tmp` 中进行，原工作区只读。

---

## 9. 禁止事项

- 不要在 `contestant_ws` 原目录直接改源码、测试、配置或已有报告。
- 不要直接删除/覆盖 `CODEX_REVIEW_v1.1.md`、`CODEX_NEW_IDEAS_v1.1.md`、`NEW_IDEA_IMPLEMENTATION.md`。
- 不要把 `event_revisions`/`purity_joint_actions`/`opening_route_information` 直接改成默认 true；先用配对证据。
- 不要只用单张地图/单个席位/单次真实模型结果推广。
- 不要用正则/关键词解析事件；语义只由 LLM 产生。
- 不要用 `run_deepseek.sh` 或本地直连模型替代正式网关资格。
- 不要用 `strategy_eval.py` 的 `--workspace` 指向仓库内部；用 `/tmp`。
- 不要读其他队感知、裁判全图、内部接口、测试配置作为策略输入。
- 不要为了“让指标变好”修改测试断言或忽略失败批次。

---

## 10. 可用命令（只读/沙盒）

```bash
# 查看状态（只读）
git -C /home/miraculy/docker/ros2/contestant_ws status --short
sha256sum /home/miraculy/docker/ros2/contestant_ws/build/rmtc_bot/contestant

# 读 PDF（stdout，不写文件）
pdftotext -layout "<pdf path>" - | sed -n '1,200p'

# 读最新测试日志
grep -E '^\[==========\]|^\[  PASSED  \]|^\[  FAILED  \]' \
  /home/miraculy/docker/ros2/contestant_ws/build/rmtc_bot/Testing/Temporary/LastTest_*.log

# 如果必须运行，先复制到独立目录（不要用原目录）
docker exec ros2_humble_gazebo bash -lc '
  test ! -e /root/ros2_ws_handoff && mkdir -p /root/ros2_ws_handoff
  tar -C /root/ros2_ws --exclude=./build --exclude=./install \
      --exclude=./log --exclude=./test-results -cf - . | \
  tar -C /root/ros2_ws_handoff -xf -
'
```

如果跑四车评估，使用 `/tmp` 输出目录，例如：

```bash
python3 tools/strategy_eval.py \
  --judge-root /root/rmtc_practice \
  --workspace /tmp/rmtc-review \
  --binary /root/ros2_ws_handoff/build/rmtc_bot/contestant \
  --label review --variants default --seats A,B,C,D \
  --opponents self --candidate-count 1 --no-events \
  --ai off --domain 95 --port 8595
```

真实模型对照必须显式选择，且不能把练习成功当作正式资格。

---

## 11. 最终回复格式

请最终给用户：

1. 报告文件路径；
2. 3–5 条最重要结论；
3. 一张 P0/P1/P2 表；
4. 明确说明“已修改/未修改”；
5. 如果发现已有结论误报，单独列出。

报告正文用中文；证据中的代码/路径/命令保持原文。

---

## 12. 交接边界

这份任务书只提供背景与检查清单，不替你下结论。  
已有报告中的每个数字、每条判断都需要你用当前代码、规则和日志复核。  
如果发现交接文档本身有误，请直接在报告中指出，不要为了保持一致而接受它。
