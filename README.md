# RMTC C++ 参赛程序

基于 C++17、ROS 2 Humble 的自主参赛程序，支持第一阶段资源采集，以及第二阶段资源争夺、公开任务、对抗和异步 LLM 事件理解。

## 1. 仓库入口和启动目录

以下构建、启动和检查命令均从**本仓库根目录**执行，即同时包含 `src/`、`stage2/`、`build_contestant.sh` 和 `run_contestant.sh` 的目录。本项目本身就是 ROS 工作空间，不需要再进入一层 `contestant_ws`。

```bash
# 首次获取代码；已有仓库时直接进入自己的仓库目录
git clone https://github.com/Miraculy/RMTC_coding.git
cd RMTC_coding
```

使用维护者的现有目录时，宿主机仓库根目录为 `/home/miraculy/docker/ros2/contestant_ws`；Docker 内挂载为 `/root/ros2_ws`。这些是维护者环境路径，其他机器按自己的克隆位置进入仓库。

| 阶段 | 从仓库根目录启动 | 使用的源码和安装产物 | 标准比赛长度 |
| --- | --- | --- | --- |
| 第一阶段 | `./run_contestant.sh A 1 -p 'team_name:=你的队名'` | `src/rmtc_bot/` → `install/rmtc_bot/`，根包 e075 算法 | 100 ms/Tick，100 Tick；动作 Tick 0–99 |
| 第二阶段 | `./run_contestant.sh B 2 ...`，B 替换为分配席位；完整配置见第 5 节 | `stage2/src/rmtc_bot/` → `stage2/install/rmtc_bot/`，独立二阶段发布包 | 500 ms/Tick，500 Tick；动作 Tick 0–499 |

脚本第一个位置参数是席位 `A/B/C/D`，第二个是阶段 `1/2`。第一阶段按手册固定使用 A；第二阶段使用本场分配席位。两阶段切换时停止旧选手进程，再启动对应阶段；同一席位只允许本队一个控制进程，不能与参考车或陪练重复占用。

## 2. 运行环境和依赖

- 推荐 Ubuntu 22.04，必须兼容 ROS 2 Humble；使用 Bash 执行本文命令。
- C++17 编译器、CMake、colcon、`ament_cmake`。
- ROS 依赖：`rclcpp`、`std_msgs`、`rmw_fastrtps_cpp`。
- C++ 库：nlohmann/json、libcurl；测试依赖 `ament_cmake_gtest`。
- 官方预编译练习裁判面向 x86_64 Linux；练习裁判由主办方另行提供，不包含在本仓库中。

先安装 ROS 2 Humble，确认 `/opt/ros/humble/setup.bash` 存在，再安装本仓库依赖：

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake python3-colcon-common-extensions \
  ros-humble-ament-cmake ros-humble-rclcpp ros-humble-std-msgs \
  ros-humble-rmw-fastrtps-cpp ros-humble-ament-cmake-gtest \
  nlohmann-json3-dev libcurl4-openssl-dev
```

使用 Python 开发工具或练习裁判时，另按对应说明准备依赖；常用项为 `ros-humble-rclpy`、`python3-requests`、`python3-yaml`。`tools/verify_stage2_release.py` 需要 PyYAML。容器内以 root 安装时去掉 `sudo`。到场前完成依赖安装和构建，避免依赖现场下载。

Docker 为可选运行方式。维护者环境的进入方法：

```bash
docker exec -it ros2_humble_gazebo bash
cd /root/ros2_ws
```

现有 Compose 使用 host 网络，静态 IP 在宿主机有线网卡设置，ROS 和 AI 环境在运行程序的容器终端设置。虚拟机通常需要桥接到物理有线网卡，并在运行 ROS 的 Ubuntu 来宾中核对分配 IP；宿主机与来宾不能重复使用同一个静态 IP。SSH 可访问不代表 DDS 已双向连通。更多本地环境说明见 [开发环境记录](docs/DEVELOPMENT_NOTES.md)。

## 3. 目录、主要模块和运行逻辑

```text
仓库根目录/
├── src/rmtc_bot/                 # 根包：正式第一阶段使用
│   ├── include/rmtc_bot/         # Frame、World、Planner 和 AiClient 接口
│   ├── src/node.cpp              # ROS 参数、订阅/发布、场次管理和日志
│   ├── src/core.cpp              # 感知解析、地图记忆和公共规划逻辑
│   ├── src/stage1_planner.cpp    # 已知资源路线和预算内收尾
│   ├── src/stage1_joint.cpp      # 资源与视野联合评估
│   ├── src/stage1_subgoal.cpp    # 子目标与假想场景评估，含 e075 排序
│   ├── src/ai.cpp                # 异步模型客户端；第一阶段不启用
│   ├── config/contestant.yaml    # 手动加载的开发模板
│   └── test/                     # 根包核心和本地 HTTP 测试
├── stage2/
│   ├── src/rmtc_bot/             # 独立二阶段 node/core/ai 源码及测试
│   ├── config/final.yaml         # 当前53项正式策略参数
│   ├── SOURCE_MANIFEST.json      # 当前源码和策略配置校验清单
│   └── COLCON_IGNORE             # 避免根工作空间扫描到同名包
├── tools/                       # 开发联调、评估和审计脚本
├── docs/                        # 既有策略说明及历史验证记录
├── build_contestant.sh           # 依次构建一阶段和二阶段
├── build_stage2.sh               # 单独构建二阶段
├── run_contestant.sh             # 根据阶段分派启动入口
├── run_stage2_final.sh           # 直接运行独立二阶段安装产物
└── run_deepseek.sh               # 本地练习直连工具，正式比赛不用此入口
```

程序主流程：

1. `node.cpp` 读取席位、队名、阶段、策略及 AI 参数，发布本席位队名注册消息。
2. 订阅本席位感知，检查 JSON 字段类型；使用本帧 `tick`、`session_id` 和 `self.position`。实体的 9×9 相对位置图转换为本地规划坐标。
3. `World` 保存本车合法感知形成的地图、资源、敌人和事件记忆。新场次清空上场世界、规划和动作缓存；旧 Tick 丢弃，同 Tick 重发只重发缓存动作，不重复推进规划。
4. 第一阶段由 e075 规划器比较采集和探索路线，只提交 MOVE/WAIT；第二阶段由独立规划器比较资源、任务、探索、事件和战斗候选。
5. 第二阶段 `AiClient` 在后台线程调用统一网关。模型结果核验所属场次和结构后进入事件记忆；超时、配额耗尽或内容被拒绝时继续本地决策，不阻塞逐 Tick 控制。
6. 发布合法动作，原样回填当前感知的 Tick 和场次 ID；通过下一帧 `self.last_action` 查看实际执行结果，并记录本队 JSONL 日志。最终状态 Tick 100/500 不再发布新动作。

程序仅使用本队专属感知和协议允许的公开信息，不把裁判全图、其他席位私有数据或观赛画面作为策略输入。当前实现的已知限制见第 8 节。

### 3.1 第一阶段每 Tick 执行流程

本节另有可独立阅读的[第一阶段每 Tick 执行流程](docs/STAGE1_TICK_FLOW.md)。

当前正式入口是 `./run_contestant.sh A 1`，运行根工作空间的 `install/rmtc_bot/` 安装包。脚本设置 `team=A`、`stage=1`、`max_ticks=100`，不自动加载开发模板 YAML；三个第一阶段规划开关使用根包 `node.cpp` 中的默认值 `true`。以下按没有额外参数覆盖时说明，标准节奏为100 ms/Tick。

一个正常新 Tick 的顺序是：**收到感知 → 校验和更新状态 → 判断是否复用目标或进入收尾 → 必要时重做候选推演 → 确定本步 MOVE/WAIT → 发布并缓存动作 → 写日志。** 程序由感知消息触发，收到后立即处理，不等待100 ms用满；第一阶段不创建 AI 客户端，不调用模型，也不等待模型解释。

**1. 检查这帧能不能处理**

先解析 JSON，检查字段类型、地图坐标、队伍席位、`session_id` 和 `tick`，再按帧的状态分流：

| 收到的帧 | 处理方式 |
| --- | --- |
| 正常的新 Tick | 更新状态并决策 |
| 当前 Tick 的重复帧 | 重发该 Tick 的缓存动作，不更新历史，也不重新规划 |
| 更早的 Tick | 丢弃 |
| 已退出的旧场次 | 丢弃，避免旧消息重新激活 |
| 新场次 | 清空上一场的地图、资源、访问记录、目标、路线和规划缓存 |
| 格式错误或席位不匹配 | 记录拒绝，不猜测动作 |

默认 Tick 0–99 可以发布动作；新收到 Tick 100 或更大的结束帧时，更新并记录最终状态，随后返回，不再调用规划器或发布新动作。

**2. 用实际感知更新自己的状态**

以裁判给出的实际位置、存活状态和资源累计价值为准，把实体相对位置转换成地图坐标，维护已观察格子、障碍、资源、访问次数及历史帧。当前视野范围根据感知中的 `vision_radius` 计算。

以前看见、目前在视野外的资源继续保留；重新看到其位置且资源确实不存在时才清除。程序不会因上一 Tick 发出向右移动就自行把位置右移，而是从本帧真实位置重新规划；`self.last_action` 同时记入日志，供核对实际执行结果。

普通、高值资源的已采数量只在连续两帧、自身存活、当前位置有先前已见资源且权威资源价值增加等条件下确认，不会仅凭总价值猜测采集类型。剩余预算为 `max_ticks - 当前 tick`，例如 Tick 20 时默认剩80个行动 Tick。

**3. 判断本 Tick 应该走哪条规划分支**

每个有效新 Tick 都进入 `Planner::decide`；死亡时直接 WAIT。正常默认调度顺序为：没有受保护收尾路线时先尝试子目标风险规划，再尝试资源与视野联合规划，然后尝试已知资源路线规划；仍无法给出动作时使用通用规划器。某个分支返回动作后，主入口就停止向下尝试。子目标规划内部还会调用其他模块取得候选或判断是否收尾。

主要有三种计算量不同的情况：

| 当前情况 | 本 Tick 主要做什么 |
| --- | --- |
| 已有受保护的资源收尾路线 | 优先进入资源路线模块，检查剩余资源、时间和可行收益，求本步方向；仍可能重新运行子集动态规划，不是单纯取缓存 |
| 子目标仍有效，且没有新知识 | 复用旧目标，从当前实际位置通过已观察空地求最短路径，预算允许时取第一步；跳过本次8场景推演 |
| 没有可复用目标，或出现新知识、到达目标、路线不再可行 | 重新生成候选并比较，必要时转入资源收尾或回退其他规划器 |

“没有新知识”由已观察格子、墙、资源和已采价值形成的签名判断；位置和访问次数本身不在这个签名中，因此沿已知走廊正常移动不一定触发完整重算。但目标必须仍有非空可行路径，且剩余 Tick 足够。

**4. 需要完整重规划时，比较采集和侦察候选**

完整子目标评估先计算只沿已观察空地行动时，已知资源能兑现的收益。剩余已知资源不超过12个时采用精确子集计算，更多时采用多起点贪心可行路线。所有剩余资源已发现，或者已采价值加当前可兑现的已知价值达到190R时，优先尝试资源路线收尾；该尝试仍需满足路线模块自身条件。

没有直接转入收尾时，继续计算：

1. 生成原有控制器参考动作、联合规划目标、已知资源路线第一段，以及其他资源候选。
2. 从已观察可通行位置中选取最多三个侦察候选；计算沿途视野并集，以边际新增视野除以 `(距离+1)^0.75` 排序。`0.75` 是 e075 的候选排序指数；它比 `0.85` 更弱地惩罚距离，不表示只选择最近目标。
3. 生成8张假想地图。保留真实已观察信息，按已见障碍密度采样未知墙，并按剩余标准资源数量放置假想资源；每个候选在同一组样本上模拟至剩余预算结束。
4. 推演中的控制器也只观察自己的方形视野，先尝试候选路径，再用简化的采集和探索策略续走。假想墙、资源和预测视野不会写入真实 `World`。
5. 比较平均采集收益、相对参考策略最差两个样本的平均损失，以及走完候选后已知资源可兑现价值的损失。涉及已知图狭窄分支的候选，还会补做未知出口封闭的压力推演。
6. 常规样本最差损失或压力场景损失超过10R的非参考候选不接受；其余按评分选目标并缓存本次评估。这个10R门槛只约束模型中的比较，不保证真实比赛最多损失10R。

联合规划模块会把真实已知资源收益、沿途新增视野、假想资源取回成本和未知区域绕路代价放在同一条路线中估值，并用插入观察点、路线片段反转等方法改进顺序。它既可提供子目标候选，也可在主入口单独接管回退情况。

资源路线模块则用 BFS 距离和最多16个资源的子集动态规划，寻找剩余时间内的高价值采集顺序，同价值优先较短路线；计算有耗时检查，未完整求解时保留已找到的可行方案。它还比较参考后续的乐观收益上界，满足当前10R判据时形成受保护路线；这不等于已求得未知全图的全局最优解。

**5. 确定这一 Tick 的 MOVE 或 WAIT**

专用规划器通常直接返回所选路径的第一步，不执行第二阶段那套攻击、防御收益比较。已知资源收尾只使用已观察可通行路线；等长的下一步优先考虑新视野，其次考虑较少访问的格子。联合规划即使使用了未知区域的路径估计，对外发出的下一格也必须已经观察且可通行。

脚下有待采资源时，资源路线模块可直接返回 WAIT；没有可兑现收益时也可等待。专用模块不适用时，通用规划器继续比较采集与探索方向。节点最后检查动作类型和方向；第一阶段只允许 MOVE/WAIT，非法结果替换为 WAIT。

即使完整推演已经比较了未来几十步，本 Tick 也只提交一个动作。地图尺寸、资源配置或采集确认不满足专用模块假设时会逐级回退：主子目标模块要求宽、高均不超过20，资源不超过16个，并使用12个普通10R、4个高值20R、总量200R的标准假设。

**6. 发布并缓存本 Tick 动作**

动作原样携带当前感知的 `tick` 和 `session_id`，发布到 `/team_A/action`，并缓存完整 JSON；MOVE 额外携带方向。当前 Tick 再收到重复感知时直接重发这份缓存，不再推进路线或重复模拟。

第一阶段没有后台模型结果触发的同 Tick 动作修订。动作是否实际执行、车是否移动、资源是否采到，由后续裁判感知确认。

**7. 写日志，再等下一帧**

动作发布后，记录实际位置、资源价值、上一动作回执、当前动作、目标、选择原因和 `stage1_route_evaluation`。规划评估可以显示本次是否复用目标、采集路线价值与步数、候选和假想场景结果，以及求解是否完成；复用时记录可能沿用上次评估，并带有 `reused_leg` 标记，不表示本 Tick 又做过全套推演。

`latency_ms` 在动作发布后、整理并写入后续决策日志前取值，不包含后续完整日志处理时间，也不表示裁判已收到或执行动作。下一帧到来后重新确认实际局面，再走上述流程。

各规划模块的耗时检查与日志里的已测耗时，不能当成整个感知回调的硬超时保证。本文描述当前源码执行分支，不新增本次时延或比赛验证结论。

### 3.2 第二阶段每 Tick 执行流程

本节另有可独立阅读的[第二阶段每 Tick 执行流程](docs/STAGE2_TICK_FLOW.md)。

当前正式入口是 `./run_contestant.sh <席位> 2`，它转入 `run_stage2_final.sh`，加载 `stage2/config/final.yaml`。以下按该配置、没有额外参数覆盖时说明；默认比赛长度为500 Tick，标准节奏为500 ms/Tick。

一个正常新 Tick 的顺序是：**收到感知 → 校验和更新状态 → 合并已完成的 AI 解释 → 尝试提交新公告 → 评估目标和动作 → 发布并缓存动作 → 写日志。** 感知到达后立即计算，不等待500 ms用满；AI HTTP 请求由后台线程执行，本地决策不等待模型返回。

**1. 检查这帧能不能处理**

先解析 JSON，检查字段类型、坐标、队伍席位、`session_id` 和 `tick`，再按帧的状态分流：

| 收到的帧 | 处理方式 |
| --- | --- |
| 正常的新 Tick | 更新状态并决策 |
| 当前 Tick 的重复帧 | 重发该 Tick 的缓存动作，不重新规划，也不再次提交 AI 请求 |
| 更早的 Tick | 丢弃 |
| 已退出的旧场次 | 丢弃，避免旧消息重新激活 |
| 新场次 | 清空上场世界和规划缓存，重置 AI 会话并重新读取凭据；相同令牌不会因场次切换而重置调用计数 |
| 格式错误或席位不匹配 | 记录拒绝，不猜测动作 |

默认 Tick 0–499 可以发布动作；新收到 Tick 500 或更大的结束帧时，更新状态、收取已完成的 AI 结果并记录最终状态，随后返回，不再提交新请求或规划新动作。

**2. 用实际感知更新自己的状态**

更新本车实际位置、HP、存活和保护状态、权威资源价值、有效伤害及已完成任务；把相对位置图转换成地图坐标，维护已观察格子、障碍、资源、敌人记忆、访问次数和历史帧。

曾见资源在视野外仍保留；重新看到其位置且资源不存在时才清除。程序不因上一 Tick 发过 MOVE 就假定移动成功，而是结合本帧位置和 `self.last_action` 判断实际结果。已启用的旧资源受阻恢复只在前期、无已解释事件等适用条件下处理连续失败证据，不能等同于通用移动脱困；后者当前关闭。

剩余预算为 `max_ticks - 当前 tick`。例如 Tick 120 时，默认还剩380个行动 Tick；目标和路线需要在这个预算内比较。

**3. 收取已经完成的 AI 解释，不等待在途请求**

感知回调在规划前调用 `collect_ai(false)`，只收取后台已经完成的结果：

- 丢弃不属于当前场次的结果。
- 对 HTTP 成功、内容可解析的结果，继续检查事件结构、时间、坐标等；通过后才合并到事件记忆。
- 结构校验失败时，在次数和预算允许的条件下排入修复请求；错误、截断或不可用内容不作为有效解释使用。
- 记录 HTTP 状态、内容是否接受、错误分类、耗时及用量，然后继续本地控制。

本地 `ai_success_` 表示已有 HTTP 成功响应，不等于事件结构已通过，也不能代替裁判的正式 AI 资格判定。

节点另有20 ms周期的 AI 结果收取定时器，但它和感知回调运行在同一个 ROS 执行线程，不能打断正在进行的规划。当前 `ai_action_revision=false`：模型稍后返回时可以更新事件记忆，但不会因此改发当前 Tick 的动作，通常供下一次新 Tick 决策使用。

**4. 尝试把新公告提交给后台 AI**

感知中的 `event_text` 按行处理；当前代码只提交包含“预告”的非空行。公开事件话题也可在两个感知 Tick 之间触发提交，并使用同样的筛选条件。AI 客户端按原文去重，避免同一公告从话题和感知重复调用。

尚无 HTTP 成功记录时，节点还会按条件尝试提交初始任务分析请求；排队仍受令牌、去重、队列容量和预算约束，并非每 Tick 都实际调用模型。HTTP 工作在后台进行，本 Tick 不等待它完成。缺少令牌、请求超时或额度耗尽时，继续使用已有观测和已接受事件解释决策。

“预告”是当前实现中的固定词筛选，不是协议对所有公告句式的保证；不含该词的合法公告可能漏交。这里说明现有行为，不代表该兼容缺口已经修复。

**5. 根据当前状态重新评估“去哪里”**

每个有效新 Tick 都调用一次第二阶段规划器；死亡时直接 WAIT，世界分裂强制复活或已解释环境伤害使当前 HP 耗尽等情况也可能提前返回。正常情况下继续计算：

1. 用带权最短路搜索计算可达目标、路线成本和第一步。成本包含步数、未知格子代价、当前可见敌人的同线暴露及已解释事件风险。当前被机器人占据的下一格不能进入；较远的占位格可以作为未来可能腾出的路线假设。
2. 比较已知资源、公开采集任务、坐标任务、探索目标、回血点、堡垒和交战位置等收益；没有合适目标时再考虑追踪或巡逻。候选需要可达且剩余时间足够，旧目标有延续奖励，但仍参与本 Tick 的重新比较。
3. 默认 `resource_lookahead=3`，对最多16个资源候选比较最多三个资源点的短序列，并用 `resource_arrival_value=true` 估计到达时资源仍可取得的概率。当前 `resource_paths_exact=false`，第一段使用本地路径成本，后续资源间距离采用曼哈顿估计；这不是全局最优采集证明。
4. 开局 Tick<40 时，对出生象限内已观察到的20R及以上资源增加偏好。满足前期安全、资源数量等限制时，观察绕行可用至多2步额外路程换取视野，当前每场至多尝试1个不同目标；它不把未知格子直接认定为有资源。
5. 只有已经通过校验的事件解释参与规划。天使回血结合缺血量、路程和有效时间估值；堡垒结合可达性和驻留时间提供候选。`event_timing`、`fortress_time_budget` 当前关闭，不能把实验分支的完整时间规划算成已启用行为。

第二阶段没有第一阶段的8张假想地图抽样流程。观察绕行和受阻恢复可以保留局部计划，但主规划器仍会在新 Tick 中重新评估目标及动作。

**6. 比较“这一 Tick 具体做什么”**

选出目标和偏好移动方向后，还要进行即时动作比较，目标方向不保证直接成为最终动作。候选包括：

- WAIT。
- 四个方向的 MOVE；越界、已知墙、当前被占据的落点等会被筛掉，幻觉状态会改变 MOVE 的实际方向。
- 非保护状态下的四个方向 ATTACK。
- 仅在 `purity_defense=true` 且已解释的“洁身”事件当前生效时加入 DEFEND；普通时段不生成防御候选。

评分综合目标推进、资源或任务的原地完成机会、重复访问惩罚、预计输出和受到的伤害、击杀奖励、死亡扣分、保护与事件效果；满足条件的对峙和追击循环处理也会参与。移动的收益不能抹掉本 Tick 移动前已经发生的战斗伤害。

`score_aware_combat=true` 时，程序用500点对抗项门槛、死亡扣60及击杀奖励等规则做假想收益估值，同时考虑满分后的缓冲和同分比较价值；实际有效伤害及任务完成情况始终采用裁判感知，不用这些预测覆盖权威值。最终选择评分最高的合法动作，节点再次校验，不合法则替换为 WAIT。

**7. 发布动作、写日志，再等下一帧**

动作原样携带当前感知的 `tick` 和 `session_id`，发布到 `/team_<席位>/action`，并缓存其完整 JSON。下一次收到相同 Tick 的感知时直接重发缓存。

发布后记录本队动作、上一动作回执、实际位置、得分、目标及原因，以及触发时的观察绕行、资源恢复等评估日志。`latency_ms` 在动作发布后、整理并写入后续决策日志前取值，不包含后续完整日志处理时间，也不表示裁判已收到或执行动作。

下一 Tick 以新感知确认实际执行结果，再走上述流程。即使当前评分包含后续路线、资源序列或事件收益预测，对外也只提交当前 Tick 的一个动作。

## 4. 构建、产物路径和版本确认

在仓库根目录执行：

```bash
./build_contestant.sh
```

脚本先构建根包，再执行 `build_stage2.sh`；两阶段分别生成自己的 `build/`、`install/`、`log/`。根包默认为 `RelWithDebInfo`，二阶段为 `Release`。只重建二阶段可执行：

```bash
./build_stage2.sh
```

一阶段的等价手动构建命令：

```bash
source /opt/ros/humble/setup.bash
colcon build --base-paths src --symlink-install --packages-select rmtc_bot \
  --cmake-args -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

换机器或移动工作空间后应重新构建，不能直接复用其他路径下的生成目录。构建后修改源码不会自动更新已运行进程；按裁判允许的流程重新构建和启动。

版本确认时，在本仓库根目录检查并记录：

```bash
pwd
git rev-parse --show-toplevel
git rev-parse HEAD
git status --short
test -x install/rmtc_bot/lib/rmtc_bot/contestant
test -x stage2/install/rmtc_bot/lib/rmtc_bot/contestant
sha256sum install/rmtc_bot/lib/rmtc_bot/contestant \
  stage2/install/rmtc_bot/lib/rmtc_bot/contestant
python3 tools/verify_stage2_release.py
```

确认实际提交版本与启动源码路径一致，并核对未提交修改、未跟踪文件及实际需要的源文件是否都已纳入交付。Git 提交号不能单独证明安装产物来自该源码；需要从确认后的源码构建并记录实际可执行文件。

根包还可在干净的 Bash 终端确认包来源：

```bash
source /opt/ros/humble/setup.bash
source ./install/setup.bash
ros2 pkg prefix rmtc_bot
```

其结果应指向本仓库的 `install/rmtc_bot`。二阶段启动脚本直接执行本仓库 `stage2/install/rmtc_bot/lib/rmtc_bot/contestant`，避免其他 ROS overlay 的同名包被误用；直接运行根包的 `ros2 run rmtc_bot contestant` 不等同于启动独立二阶段发布版。二阶段未构建时入口报错，不回退旧程序。

阶段调整窗口结束后重新确认第二阶段版本，并按手册核对 Git 状态。调整及重启仅在裁判允许的窗口和流程内进行；本文命令本身不赋予赛中调参权限。当前没有 `decision_config.json` / `decision_type` 枚举切换功能，不使用人工策略切换时无需该文件。

## 5. 正式比赛接入与完整启动命令

### 5.1 有线 IP、队伍身份与本场信息

按工作人员通知接入指定网口，并使用分配的静态 IPv4 地址和子网掩码。Ubuntu 在“设置 → 网络 → 对应有线连接 → IPv4 → 手动”填写；手册操作步骤要求网关和 DNS 留空，具体仍以分配的接入信息为准。应用后重新连接，检查地址生效。

**设备 IP、长期队伍 ID、本场 A/B/C/D 席位分别管理。** 不根据视频示例或队伍编号自行拼 IP。同一网络规划下仅换席位不需要重改设备 IP；工作人员重新分配时才修改。手册中的 `192.168.50.11/24` 也只是示例。

```bash
ip -4 -brief address
# 替换成实际裁判机地址；不要把星号作为地址执行
read -r -p '本场裁判机 IP: ' referee_ip
ip route get "$referee_ip"
ping -c 3 "$referee_ip"
```

路由应使用比赛有线网卡。Ping 只辅助判断 IP 连通性，最终仍以感知、动作和执行回执确认。

本项目的身份参数：

| 项目 | 本项目的配置方式 |
| --- | --- |
| 队伍显示名称 | `-p 'team_name:=你的队名'`，默认“图灵探索者”，1–96 UTF-8 字节，不含换行 |
| 本场席位 | `run_contestant.sh` 第一个位置参数；一阶段 A，二阶段按分配值 |
| 长期队伍 ID | 由裁判正式名单与场次映射确认，本项目没有 `team_id` 参数 |
| 注册消息 | 发布到 `/team_X/registration`，内容为 `{"team":"X","name":"你的队名"}`；只登记显示名称 |
| 场次与 Tick | 自动读取当前感知，不手工填写或沿用上场值 |

按手册，本队动作 JSON 无须携带队名或长期 ID，也无须填写裁判 IP；ROS 根据域和话题通信。参考程序的 `team_id`、命名空间、`mode` 等启动参数不能直接套到本项目。

### 5.2 在实际运行程序的终端设置本场环境

下列 `27` 和地址占位符都必须按本场接入单替换。双裁判赛场各有自己的域、裁判地址和网关，不能只看席位字母判断所属赛场。

```bash
source /opt/ros/humble/setup.bash
export ROS_DOMAIN_ID=27
export ROS_LOCALHOST_ONLY=0
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
```

程序终端和诊断终端均须设置相同的本场环境。脚本会继承已有环境值，尤其不会纠正已有的 `ROS_LOCALHOST_ONLY=1`。修改环境变量不改变已运行节点的环境，换域后按裁判安排重新启动程序。

### 5.3 第一阶段：A 席位，100 Tick

从仓库根目录、已设置本场 ROS 环境的终端启动：

```bash
./run_contestant.sh A 1 -p 'team_name:=你的队名'
```

脚本加载根安装环境并设置 `stage=1`、`max_ticks=100`。第一阶段不调用 AI，无须配置模型、网关或令牌；程序仅提交 MOVE/WAIT。

### 5.4 第二阶段：本场席位、官方网关和新令牌

手册第 5 页指定正式模型为 **`deepseek-flash`**，请求字段 **`max_tokens=512`**。本程序对应 ROS 参数为 `ai_model` 和 `ai_max_tokens`。当前代码的输出上限默认仍为 2500，正式启动必须显式覆盖为 512；下面命令完成覆盖，无须修改策略配置或源码。

在实际运行程序的终端设置：

```bash
export RMTC_AI_PROVIDER=gateway
export AI_GATEWAY='http://<本场裁判机IP>:<端口>/v1/chat/completions'
export RMTC_AI_MODEL=deepseek-flash
unset DEEPSEEK_API_KEY
read -r -s -p '请输入本场席位令牌: ' RMTC_TEAM_TOKEN
echo
export RMTC_TEAM_TOKEN
```

`AI_GATEWAY` 使用工作人员发放的**完整请求地址**，不能沿用 `127.0.0.1:8080` 或示例 IP。随后从仓库根目录启动，例如本场分配 B：

```bash
./run_contestant.sh B 2 \
  -p 'team_name:=你的队名' \
  -p ai_provider:=gateway \
  -p ai_model:=deepseek-flash \
  -p ai_max_tokens:=512
```

脚本设置 `stage=2`、`max_ticks=500` 并加载 `stage2/config/final.yaml`。该文件不包含队名、网关、模型或输出上限；相应值由节点默认值、环境和显式参数提供。HTTP 客户端发送非流式 `messages` 请求及 Bearer 鉴权。正式比赛只使用主办方统一网关，`run_deepseek.sh` 是本地练习工具。

| AI 参数 | 当前代码默认值 / 读取来源 | 正式接入使用方式 |
| --- | --- | --- |
| `ai_provider` | `RMTC_AI_PROVIDER` 或 `gateway` | 显式使用 `gateway` |
| `ai_gateway` | `AI_GATEWAY` 或本机练习地址 | 设置为本场完整 `/v1/chat/completions` 地址 |
| `ai_model` | `RMTC_AI_MODEL` 或空值 | 按本手册使用 `deepseek-flash` |
| `ai_max_tokens` | 2500 | 按本手册显式覆盖为 512 |
| `ai_enabled` | `true`；第一阶段强制不启用 | 二阶段保持启用 |
| `ai_timeout_ms` | 12000 | 独立工作线程超时，不阻塞逐 Tick 动作 |
| `ai_budget` | 90 次本地成功预算；0 表示本地不限制 | 与本场配额核对，不要求用满；网关仍会限额 |
| `token_file` | 空时读取 `RMTC_TEAM_TOKEN` | 可选私密文件；指定后优先读文件，须同步更新本场令牌 |

令牌不写入仓库、公开命令记录或截图。节点在新场次读取令牌；对已经运行的节点，在另一个 shell 中 `export` 新令牌不会生效。通常在新场次启动前更新当前终端环境，并按允许流程重启；使用私密文件时在新场次读取前更新文件。

后置 `-p` 或 `--params-file` 可以覆盖前面的参数。`src/rmtc_bot/config/contestant.yaml` 和 `stage2/src/rmtc_bot/config/contestant.yaml` 是开发模板，含练习地址和2500输出上限，正式启动不要额外加载这些模板覆盖本场配置。

### 5.5 确认接入和双裁判换场

看到 `C++ contestant ready` 只说明节点完成初始化。完整双向验证应在指定联调场次完成：收到本席位的感知 → 本队程序提交合法动作 → 后续 `self.last_action` 出现对应动作及 Tick 回执。观察话题列表、网页或感知单向增长不能代替该链路。

辅助诊断可在另一个已设置相同 ROS 环境的终端执行，B 替换为本队实际席位：

```bash
source /opt/ros/humble/setup.bash
# 按本场替换；与程序终端保持一致
export ROS_DOMAIN_ID=27
export ROS_LOCALHOST_ONLY=0
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
ros2 topic list --no-daemon --spin-time 5
ros2 topic echo /team_B/perception std_msgs/msg/String \
  --qos-reliability reliable --qos-durability volatile
```

本队席位为 B 时，程序订阅 `/team_B/perception`，发布 `/team_B/action`、`/team_B/registration`，并订阅 `/referee/mission`、`/referee/event_text`、`/referee/team_registry`。全部为 `std_msgs/msg/String`；感知与动作使用 RELIABLE / VOLATILE / KEEP_LAST(1)，其他话题 QoS 按协议设置。

正式第二阶段须在最终结算前取得**本场、本席位至少一次真实统一 AI 成功调用记录**，由裁判确认。否则任务计分数 `M=0`，原始任务数也不作为同分优势，资源与对抗分保留。`/healthz` 可用、车辆能移动或本地 HTTP 成功提示均不能代替正式审计；练习或联调调用不能预先取得正式场次资格。

换场时重新核对所属赛场、ROS 域、席位、网关和新令牌；设备 IP 在未重新分配时保持。代码、Prompt 和决策参数仅在裁判明确开放的统一调整窗口内修改，随后重新确认版本；后续每轮窗口安排按正式通知执行，不自行推断已获修改许可。

## 6. 公开测试端联调

以下为**本机练习**，不登记正式成绩。练习裁判目录与本仓库分开，安装方法按主办方测试教程执行。

终端 1，在练习裁判目录启动第一阶段：

```bash
source /opt/ros/humble/setup.bash
export ROS_DOMAIN_ID=27
export ROS_LOCALHOST_ONLY=0
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
cd ~/rmtc_practice
./start_referee.sh --stage 1
```

终端 2，回到本仓库根目录，设置相同 ROS 环境后启动：

```bash
export ROS_DOMAIN_ID=27
export ROS_LOCALHOST_ONLY=0
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
./run_contestant.sh A 1 -p 'team_name:=你的队名'
```

第一阶段结束后停止旧裁判和选手。在裁判目录启动第二阶段，A 留给本队，B/C/D 为陪练：

```bash
./start_referee.sh --stage 2 --bots B,C,D
```

选手终端在本仓库根目录执行：

```bash
export RMTC_AI_PROVIDER=gateway
export AI_GATEWAY=http://127.0.0.1:8080/v1/chat/completions
export RMTC_TEAM_TOKEN=practice-A
./run_contestant.sh A 2 -p 'team_name:=你的队名'
```

打开裁判机的[练习控制台](http://127.0.0.1:8080)，接入后点击“开始 / 继续”。按默认节奏验证：第一阶段 100 ms×100 Tick，第二阶段 500 ms×500 Tick；另核对暂停恢复、重开、允许流程内的程序重启、阵亡复活及异机通信。练习用后置参数覆盖不代表正式比赛允许任意调参。

默认 mock AI 仅验证请求链路，可能被事件结构校验拒绝。真实模型练习按测试教程配置 `PRACTICE_LLM_URL`、`PRACTICE_LLM_KEY`、`PRACTICE_MODEL`，经测试代理调用；如需验证本手册的输出限制，显式使用 `-p ai_model:=deepseek-flash -p ai_max_tokens:=512`。真实服务会产生费用，需自行明确选择执行。`run_deepseek.sh` 的直连调用不进入正式裁判审计。

联调及公开测试端不以正式 AI 资格门槛判定任务分，练习分数不能证明正式资格。全图观赛、网页配置、四车评估数据仅供人工开发检查，不得回传给正式控制器。异机联调的 `127.0.0.1` 指运行环境自身；网页 SSH 转发不能替代 DDS 双向通信。

## 7. 测试、日志和故障检查

构建后，从仓库根目录运行核心与本地 HTTP 测试，不调用真实付费模型：

```bash
source /opt/ros/humble/setup.bash
source ./install/setup.bash
colcon test --base-paths src --packages-select rmtc_bot
colcon test-result --verbose
colcon --log-base stage2/log test --base-paths stage2/src \
  --build-base stage2/build --packages-select rmtc_bot
colcon test-result --test-result-base stage2/build --verbose
python3 tools/verify_stage2_release.py
```

当前测试源码：根包 `core_test` 140 项、`ai_test` 14 项；二阶段 `core_test` 137 项、`ai_test` 14 项。核心测试覆盖感知、合法动作、场次和事件状态及规划边界；HTTP 测试使用本地服务，覆盖超时、重试、配额、结构拒绝和跨场异步结果等。单测通过不等于正式网络和真实 AI 接入通过。

2026-10-31 前一次只读检查运行了已有核心可执行文件：140/140、137/137 通过，并核验二阶段12个文件和53项参数清单一致；当次未重新构建或重跑 HTTP、ROS、裁判和付费模型测试。该记录不替代当前提交源码重新构建后的验证，也没有证明512输出限制下的真实事件解释完整性。

默认本队日志目录为 `~/.rmtc-bot`，可通过 `RMTC_LOG_DIR` 或 ROS 参数 `log_dir` 指定。JSONL 记录本队动作、回执、场次、得分及 AI 结果，含 `decision`、`ai`、`session`、`final` 等记录；不记录密钥、HTTP 鉴权头或思考原文。`build/`、`install/`、`log/`、`test-results/` 为忽略的生成目录。

| 现象 | 检查方向 |
| --- | --- |
| 有线网络不通 | 分配地址、掩码、重复 IP、有线网卡与到裁判机的路由 |
| 无感知 | 本队赛场、ROS 域、席位、比赛状态、QoS 和容器/虚拟机网络 |
| 有感知但不动作 | 是否重复占用席位、动作 Tick/session、动作窗口及 `self.last_action` |
| AI 401 / 404 | 本场令牌与读取方式 / 完整请求路径及本场网关 |
| AI 429 | 区分额度耗尽与限流/并发错误，继续本地控制并避免高频重试 |
| AI 内容被拒绝 | 查看 `ai` 日志的错误码；如为 `model_output_truncated`，说明输出被截断，不应在正式赛中擅自突破512限制 |
| 换场后拒绝动作 | 残留旧进程、旧域、旧席位、旧令牌或旧场次数据 |

`tools/integration.py`、`tools/wire_test.py` 含维护者容器路径，未经适配不能作为任意环境的通用入口。历史完整验证范围见 [VALIDATION.md](VALIDATION.md)，四车开发方法见 [SELFPLAY.md](docs/SELFPLAY.md)。

## 8. 当前策略和已知限制

第一阶段使用 e075；历史练习结果见 [阶段一验证摘要](docs/STAGE1_E075_VALIDATION.md)。二阶段保留 v2.4 组合，源码和53项策略参数以 [SOURCE_MANIFEST.json](stage2/SOURCE_MANIFEST.json)、[final.yaml](stage2/config/final.yaml) 为准：资源受阻恢复、观察绕行、资源到达概率、洁身条件防御、天使条件回血开启；通用移动争格恢复 `movement_blocked_recovery=false`。两种恢复开关不同，旧 `resource_blocked_recovery=true` 保留。版本沿革见 [二阶段说明](stage2/README.md)、[2026-10-30 复评](docs/STAGE2_DEEPSEEK_CONTENTION_TEST_20261030.md)，历史成绩不直接归到当前构建。
