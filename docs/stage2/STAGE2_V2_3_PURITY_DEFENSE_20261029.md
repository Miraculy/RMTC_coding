# 二阶段 v2.3：洁身自好事件期间的条件 DEFEND

日期：2026-10-29。按用户要求，在 v2.2 基础上合入“只在 purity 事件期间提供 DEFEND”的改动。

## 改动
- `PlannerConfig` 新增 `purity_defense`，默认 false；
- 二阶段默认仍不生成 DEFEND；仅当该参数为 true 且 `purity` 事件在当前 Tick active 时，才把 `defend` 加入动作候选；
- 事件结束后候选消失；
- 对防御动作增加少量基于可见攻击威胁的评分；
- 资源获取、任务目标、普通战斗与事件外行为均不变；
- `stage2/config/final.yaml` 设 `purity_defense: true` 以启用本版本行为。

## 测试
### 单元测试
- 新增 `Planner.PurityDefenseIsOfferedOnlyWhileEventActive`：purity active + 相邻敌人威胁 → 选择 defend；无事件 → 不选择 defend；
- `core_test` 137/137 通过；`ai_test` 14/14 通过。

### 封闭 3×3 测试场
测试专用地图：A 与三个陪练相邻，purity 从 Tick 1 生效，20 Tick：

| 配置 | 动作统计 | HP | 结果 |
| --- | --- | --- | --- |
| purity_defense=false | attack 9 / move 8 / wait 3 | 100→80→60→40→20→0（Tick 8） | 阵亡 |
| purity_defense=true | **defend 12** / move 5 / attack 3 | 100→80→60，之后稳定 60 | 存活 |

### 正常对局
S03/S04 真实 DeepSeek 对局中，A 在 purity 窗口内未被攻击，因此没有出现 DEFEND；两局得分/名次没有回归。该选项只在有防御收益时触发。

## 回滚
将 `final.yaml` 的 `purity_defense` 改为 false，或删除 node.cpp 中的条件动作即可恢复 v2.2 行为。

## 构建产物
- 二进制 SHA-256：`fdd175ca21f1b4b4673418b0d6047d7f2a9f1d50a2b30439c7ae9f4c72ed25ea`
