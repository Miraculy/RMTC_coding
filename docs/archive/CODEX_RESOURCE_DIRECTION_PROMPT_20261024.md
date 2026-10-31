# Codex 并行实验 Prompt：资源方向 A/B 测试

> 使用方法：把本文件分别发给两个 Codex。第一个 Codex 用 `ARM=A`，第二个用 `ARM=B`。  
> 如果你希望两个 Codex 都验证同一个 arm，也可以；但必须使用独立副本、不同的地图/席位分区，且不要互相读取结果直到两边都完成。
>
> 不要修改原始工作区 `/root/ros2_ws`。两个 Codex 各用自己的副本、自己的 ROS 域/端口、自己的 `/tmp` 输出。

---

## 0. 你的任务

你是 Codex，负责在 **RMTC v1.1** 参赛程序上做一次受控策略实验。你只做下面指定的一个 ARM，不要同时改多个策略，不要把结果直接合并到默认策略。

**本次方向：**

> 前期尽量拿到高价值资源并把资源做到 120；资源到 120 后，停止纯资源追赶，转向未完成任务、D 封顶/同分和必要的防守。  
> 不是抢光 200，也不是为普通资源送死。

两个 ARM：

- **ARM=A：开局高价值资源发现/优先**
- **ARM=B：资源 120 后的状态切换**

你只做被分配的那一个。另一个 ARM 由另一个 Codex 负责。

---

## 1. 先读这些

工作区（宿主机）：

```text
/home/miraculy/docker/ros2
```

容器内项目根：

```text
/root/ros2_ws
```

不要直接在 `/root/ros2_ws` 里改代码。先复制到自己的目录，例如：

```bash
/root/ros2_ws_codexA
/root/ros2_ws_codexB
```

必读：

- `AGENTS.md`（最新状态；注意它仍可能停在旧 build）
- `README.md`
- `VALIDATION.md` 末尾
- `docs/research/NEW_IDEA_IMPLEMENTATION.md`
- `docs/archive/CODEX_REVIEW_v1.1.md`
- `docs/archive/CODEX_NEW_IDEAS_v1.1.md`
- `docs/stage2/STRATEGY.md`
- `docs/rules/RULES_V1_1.md`
- `src/rmtc_bot/include/rmtc_bot/core.hpp`
- `src/rmtc_bot/src/core.cpp`
- `src/rmtc_bot/src/node.cpp`
- `src/rmtc_bot/test/core_test.cpp`
- `tools/opening_opportunity_audit.py`
- `tools/opening_route_audit.py`
- `tools/strategy_eval.py`、`tools/compare_eval.py`

规则重点：

- 资源分：`30 × min(R/120, 1)`；普通资源 10，高价值 20；总资源 200，四车不可能都到 120。
- 任务分：5 分/项；高价值任务和普通资源任务都可能存在；没有本场真实 AI 成功时任务分归零。
- 对抗分：`15 × min(D/500, 1)`；D 先加实际伤害和击杀奖励，再扣死亡 60，最低 0；超过 500 的 D 用于同分比较。
- 死亡成本：-60 D + 4 Tick 停机 + 回程；普通资源不值得用死亡换。
- 资源 120 后资源分不再增加；只有同分原始资源和阻止对手有意义。

---

## 2. 基线冻结

开始前记录：

```bash
cd /root/ros2_ws
git rev-parse HEAD
sha256sum build/rmtc_bot/contestant 2>/dev/null || true
```

当前预期基线：

- commit：`42bb7ce 第三轮`
- 当前工作树构建：`994515d8...`
- 文档仍可能写 `6332...`，不要用文档哈希代替实际构建。

复制到自己的目录（不要在 `/root/ros2_ws` 里构建或写日志）：

```bash
docker exec ros2_humble_gazebo bash -lc '
  test ! -e /root/ros2_ws_codexA && mkdir -p /root/ros2_ws_codexA
  tar -C /root/ros2_ws --exclude=./build --exclude=./install \
      --exclude=./log --exclude=./test-results -cf - . | \
  tar -C /root/ros2_ws_codexA -xf -
'
```

ARM=B 把 `codexA` 换成 `codexB`。

在副本里构建：

```bash
docker exec ros2_humble_gazebo bash -lc '
  source /opt/ros/humble/setup.bash
  cd /root/ros2_ws_codexA
  ./build_contestant.sh
  colcon test --packages-select rmtc_bot
  colcon test-result --verbose
'
```

先确认基线测试通过，再改代码。

---

## 3. 公共策略规则（两个 ARM 都适用）

无论你做 A 还是 B，行为都必须遵守：

1. **只使用本队合法信息**：本队 `perception`、公开 `mission`/`event_text`/`team_registry`；不读全图、对手私有感知、测试配置。
2. **不牺牲生存换普通资源**：普通资源 10 只值 2.5 分；一次死亡约 -1.8 对抗分 + 4 Tick + 回程，不值得。
3. **高价值资源优先**：20 值资源 = 5 分，并且可能推进高价值任务；前期优先。
4. **资源到 120 后停止资源分追赶**：除非为了同分/阻止对手且安全，否则转向任务、D、防守。
5. **不主动做无证据的前期远距离拦截**：前期以采集/探索为主，保留眼前可命中的攻击/必要防御。
6. **D 到 500 后不做无收益的长时间攻击/防御**：转向任务、同分、剥夺或防守。
7. **所有改动默认关闭**：新 flag 默认 `false`；不改变默认入口行为。
8. **不解析事件文本**：事件语义只由 LLM 产生；你不能加正则/关键词。
9. **不改规则/裁判**：不修改协议、裁判配置、评分公式；测试驱动只用于评估。
10. **不把单场/单图当结论**：至少 4 图 × 4 席位，保留全部掉分/掉名次。

---

## 4. ARM=A：开局高价值资源

### 目标

在比赛开始时，优先发现/拿高价值资源；如果暂时没有高价值资源可见，优先扩大视野，而不是为了远处普通资源消耗过多 Tick。

### 实现范围

- 只在第二阶段、前期生效：建议 `tick < min(60, max_ticks/5)`。
- 只改“目标选择/资源路线”这一层，不改战斗基本估值、不改资源分公式、不改默认值。
- 新 flag 建议：

```text
opening_high_value=false
```

或复用现有 `opening_route_information` 作为参考，但不要同时打开多个信息类实验。

### 行为要求

1. 已知高价值资源可见时，优先高价值资源；不要因为普通资源更近就放弃。
2. 没有高价值资源可见时，优先探索能最大新增视野的格/路线；不要为了已知远处普通资源走长路。
3. 高价值资源可见但敌人明显更近时，不硬抢；只做安全竞争。
4. 不在前期为了资源主动追击远处敌人；眼前可命中攻击/必要防御仍保留。
5. 资源达到 120 后，本 ARM 不继续追资源；交给 B 或默认任务/战斗逻辑。
6. 只用本队已知墙和可见资源；未知格可作可通行假设，不写成事实。
7. 不要用地图固定坐标/席位字母硬编码；不要使用隐藏地图。

### 建议验证

- 先跑 `opening_opportunity_audit.py` / `opening_route_audit.py` 确认触发窗口。
- 单测：
  - 已知高价值 vs 近处普通资源：优先高价值；
  - 没有高价值可见：优先新增视野；
  - 敌人更近：不硬抢；
  - 资源 120 后：本 ARM 不再加资源权重；
  - 资源格/障碍/保护/多敌边界。
- 完整配对：
  - `opening_high_value=false` 基线 vs `true` 候选，同一冻结二进制；
  - 4 图 × 4 席位 = 16 对；
  - 2 张已知图 + 2 张未调参图；
  - 无事件、AI off，先排除模型混杂；
  - 50 ms/Tick 跑主样本；另选 1–2 场用 500 ms 确认。
- 指标：
  - 首次高价值资源 Tick；
  - 高价值任务完成率；
  - 平均资源价值/资源分；
  - 总分、任务、D、名次、严格第一；
  - 死亡数、非法动作、感知拒绝、回执异常、P95 时延。

### ARM=A 验收门槛（开始前先写死，不要看结果后改）

- 平均资源价值 +≥8（约 +2.0 资源分）；
- 高价值任务完成至少 +2/16 对；
- 平均总分不低于基线；
- 没有单图分数退步 >2.5；
- 0 非法动作、0 感知拒绝、完整 0–499、回执 Tick/类型异常 0；
- 若未达到，保持 `opening_high_value=false`，写负结果。

---

## 5. ARM=B：资源 120 后状态切换

### 目标

资源分在 120 封顶后，不再把 Tick 花在低价值资源/无意义缠斗上；转向未完成任务、保持 D、争同分/阻止对手。

### 实现范围

- 只在第二阶段、资源值 `R >= 120` 后生效。
- 新 flag 建议：

```text
resource_cap_switch=false
```

- 只改目标选择/资源 reward/任务优先，不改基础战斗规则、不改评分公式、不改默认值。
- 如果 R<120，保持原默认行为；不要提前切换。

### 行为要求

1. `R >= 120` 时，普通资源/高价值资源的“资源分 reward”置 0（同分原始资源/安全阻止可另做很小权重，但第一版建议先置 0）。
2. 优先未完成的 `reach`、`normal_resources`、`high_resources`、`resource_value` 任务；只在路线可达且存活风险可接受时执行。
3. 若任务全完成：
   - D<500：保留安全战斗/补 D；
   - D≥500：只做防守、收尾、同分/阻止，不做无收益长时间缠斗。
4. 不因为“已经 120”就无脑送死；死亡仍扣 60 + 4 Tick。
5. 不把“资源 120”当成隐藏信息；只用本队感知的 `resource_value`。
6. 标准地图上 R 到 120 很少；如果 16 对中没有触发，必须如实报告“未覆盖”，不能编造收益。

### 建议验证

- 单测：
  - R=119 → 原行为；
  - R=120/121 → 资源 reward 为 0；
  - 未完成任务优先；
  - 任务全完成 + D<500 / D≥500 两种分支；
  - 敌人接近、低 HP、保护等边界；
  - `resource_cap_switch=false` 与原默认逐字段等价。
- **资源丰富诊断场景**：
  - 标准地图可能很少触发 120；可在独立测试配置中增加资源或使用 1v0/少对手场景，仅用于验证机制，不用于胜率/正式规则结论。
  - 必须标注“诊断场景”，不能和正式地图结果混算。
- 标准完整配对（如果触发）：
  - 4 图 × 4 席位 = 16 对；`false` vs `true` 同一冻结二进制；无事件、AI off。
  - 记录触发率；没有触发就只报告机制验证。
- 指标：
  - R=120 触发场次数；
  - 120 后任务完成数；
  - 120 后死亡/掉 D；
  - 最终分、资源、任务、D、名次；
  - 总 Tick/动作分布；
  - 非法/拒绝/回执/时延。

### ARM=B 验收门槛

- 机制正确：R<120 行为逐字段等价；R≥120 资源 reward 为 0；任务优先可触发。
- 标准地图若触发：
  - 平均总分不低于基线；
  - 任务完成数不低于基线；
  - 120 后死亡/掉 D 不增加（或增加有解释）；
  - 没有单图分数退步 >2.5。
- 若标准地图不触发：只保留机制验证，不推广。
- 0 非法、0 感知拒绝、完整 0–499、回执异常 0。
- 未达到则保持 `resource_cap_switch=false`，写负结果。

---

## 6. 两个 Codex 的隔离

- Codex A 只改 `/root/ros2_ws_codexA`。
- Codex B 只改 `/root/ros2_ws_codexB`。
- 不要修改 `/root/ros2_ws`；不要覆盖对方的目录；不要 push 到远端。
- 使用不同 ROS 域/端口：
  - A：`ROS_DOMAIN_ID=105`、端口 `8605`
  - B：`ROS_DOMAIN_ID=106`、端口 `8606`
- 输出只写各自副本或 `/tmp`：
  - `/tmp/rmtc-codexA/...`
  - `/tmp/rmtc-codexB/...`
- 在两边都出报告前，不读取对方的代码/结果；只共享：
  - 基线 commit/hash
  - 地图/席位列表
  - 本 prompt
  - 公共规则

---

## 7. 完整评估命令

在每个副本内执行（路径/域/端口按 ARM 替换）：

```bash
source /opt/ros/humble/setup.bash
source /root/ros2_ws_codexA/install/setup.bash

python3 /root/ros2_ws_codexA/tools/strategy_eval.py \
  --judge-root /root/rmtc_practice \
  --workspace /tmp/rmtc-codexA \
  --binary /root/ros2_ws_codexA/build/rmtc_bot/contestant \
  --label armA-baseline \
  --variants interior64811,map54617,interior83119,map94649 \
  --seats A,B,C,D \
  --opponents self --candidate-count 1 \
  --no-events --ai off \
  --domain 105 --port 8605 \
  --param opening_high_value:=false
```

候选把 `--param opening_high_value:=true`、`--label armA-candidate`、端口相应调整。  
ARM=B 把参数换成 `resource_cap_switch`，域/端口用 106/8606。

如果 `strategy_eval.py` 的白名单不允许新 flag，需要在**副本内**把新 flag 加入白名单；不改原目录。

对照必须：

- 同一冻结二进制（先构建一次，复制/记录 SHA-256）；
- 同一地图、席位、对手、Tick；
- 基线/候选仅差目标 flag；
- `comparison.json` 保存到各批目录。

---

## 8. 结果记录

每个 ARM 写一份报告，例如：

```text
/root/ros2_ws_codexA/docs/ARM_A_REPORT.md
/root/ros2_ws_codexB/docs/ARM_B_REPORT.md
```

内容包括：

1. 基线 commit / build SHA / 测试数；
2. 改动文件、flag、默认值；
3. 单元测试清单；
4. 地图/席位/对手/域/端口；
5. 每对结果表：

```text
map, seat, baseline score/R/M/D/rank, candidate score/R/M/D/rank, delta
```

6. 聚合：均值、第一数、平均名次、资源、任务、D、死亡；
7. 触发覆盖：A 的首次高价值 Tick；B 的 120 触发场次；
8. 所有退步/反例；
9. 结论：`keep off` / `continue` / `promote to next test`；
10. 明确“未修改原工作区”。

---

## 9. 禁止事项

- 不要改 `/root/ros2_ws` 原目录。
- 不要直接改默认策略或把 flag 默认设为 true。
- 不要同时改 A 和 B 两个方向；你只做一个 ARM。
- 不要用单张地图/单个席位/单次真实模型结果推广。
- 不要读全图、对手私有感知、测试配置、网页状态作为策略输入。
- 不要用正则/关键词解析事件。
- 不要用本地直连模型替代正式网关；本阶段不需要付费 AI。
- 不要修改裁判、评分公式或测试断言来“让结果变好”。
- 不要 push 到远端；不要合并到主分支。

---

## 10. 最终回复格式

给用户的最终回复只包含：

```text
ARM: A 或 B
基线: commit / binary SHA / 测试数
改动: flag / 文件 / 默认值
测试: 单元测试 + 配对数
结果: 分数 / 资源 / 任务 / D / 名次 / 第一数 / 退步
覆盖: A 的高价值首见；B 的 120 触发
结论: keep off / continue / promote
报告路径: ...
确认: 未修改 /root/ros2_ws
```

---

## 11. 如果两个 Codex 都被分配同一个 ARM

可以，但必须：

- 各自独立副本；
- 使用不同的地图/席位分区（例如 A 用 interior64811/map54617，B 用 interior83119/map94649）；
- 不共享实现和中间结果；
- 最后只比较聚合结果，不合并代码；
- 任何发现都要保留相反结果。

---

**开始前先回复：你的 ARM、基线 commit、计划修改的 flag。**
