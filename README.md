# RMTC C++ 参赛程序

基于 **C++17 和 ROS 2 Humble** 的自主参赛程序，支持资源采集、任务规划、基础对抗，以及通过官方 LLM 网关理解随机事件。

**可以直接在 Ubuntu 上开发和运行，Docker 为可选环境。** 本仓库本身就是一个 ROS 2 工作空间，以下未特别说明的命令均在仓库根目录执行，目录名称和存放位置可以自行选择。

## 当前版本状态（2026-10-27 更新）

- **第一阶段默认算法已切换为 e075**：侦察候选排序由 `marginal_view/(distance+1)^0.85` 改为 `0.75`（偏近处、可快速兑现的视野收益）。`run_contestant.sh A 1` 不传任何额外参数即启用；如需回退旧行为，显式传 `-p stage1_resource_route:=false -p stage1_joint_coverage:=false -p stage1_subgoal_risk:=false`。
- **第二阶段最终交付选择 B1**：完整17af稳定源码＋`resource_blocked_recovery=true`，保留原攻防、追击保护及模型事件解释。以排名为目标选择既有已测配置；不启用已退步的探索实验。源码独立保存在 `stage2/src/`，`run_contestant.sh A 2` 自动使用该版本和冻结参数。选择依据及局限见 [二阶段最终交付](docs/STAGE2_FINAL_RELEASE_20261027.md)。
- **阶段一验证（真实练习裁判）**：19 张 20×20 开发图与 Codex v2 逐图一致（35.6842，满分 4）；另在 4 组各 30 张独立图（`stage1_eval.generate` 由练习裁判模板生成的 18×18、25%/30% 障碍图）上复核：合并 139 局 **+0.1295**，满分 **41→43**，仅 1 张 −2（未超单图 2 分门槛），低负载 P95 ≤23.7 ms。摘要与复现方法见 [阶段一 e075 验证摘要](docs/STAGE1_E075_VALIDATION.md)。
- 改动前的正式包与本版备份、逐图原始数据与安装记录另存档于仓库外维护目录 `../analysis_opt_order_20261025/`（`INSTALL_LOG.md`、`REPORT.md`、`versions/e075/`），不随仓库提交。

## 环境要求

- Ubuntu 22.04、ROS 2 Humble。
- 支持 C++17 的编译器、CMake 和 colcon。
- `rclcpp`、`std_msgs`、Fast DDS、nlohmann/json 和 libcurl。
- 若需要运行主办方提供的预编译练习裁判，还需满足其 **x86_64 Linux** 要求。

先安装 ROS 2 Humble，确认 `/opt/ros/humble/setup.bash` 存在，再安装项目依赖：

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake python3-colcon-common-extensions \
  ros-humble-ament-cmake ros-humble-rclcpp ros-humble-std-msgs \
  ros-humble-rmw-fastrtps-cpp ros-humble-ament-cmake-gtest \
  nlohmann-json3-dev libcurl4-openssl-dev
```

练习裁判及 Python 开发测试脚本另外需要：

```bash
sudo apt install -y ros-humble-rclpy python3-requests libcurl4
```

容器中若使用 root 用户执行安装命令，去掉 `sudo` 即可。

## 获取代码与构建

克隆或复制仓库后，进入其根目录，应能看到 `src/`、`build_contestant.sh` 和 `run_contestant.sh`。首次构建执行：

```bash
./build_contestant.sh
```

脚本会自动加载 ROS Humble 环境，依次构建根目录的一阶段包与 `stage2/` 中的二阶段发布包。只构建二阶段可运行 `./build_stage2.sh`。也可以分别构建：

```bash
source /opt/ros/humble/setup.bash
colcon build --base-paths src --symlink-install --packages-select rmtc_bot \
  --cmake-args -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
./build_stage2.sh
```

一阶段生成目录为 `build/`、`install/`、`log/`；二阶段分别为 `stage2/build/`、`stage2/install/`、`stage2/log/`。`stage2/COLCON_IGNORE` 防止根工作空间自动扫描到同名包。换机器或更改工作空间位置后，重新构建，不要直接复用其他成员的生成目录。

> 说明：本 README 中阶段一 e075 的逐图分数由 **Release** 构建实测；通用脚本对一阶段仍默认使用 `RelWithDebInfo`，对二阶段使用 `Release`。附加的 CMake 参数会传给两次构建；只调整二阶段时使用 `build_stage2.sh`。

## 运行参赛程序

每个运行裁判或选手的终端都应设置一致的 ROS 网络环境。以下是**本机练习**示例，正式比赛按主办方通知替换：

```bash
export ROS_DOMAIN_ID=27
export ROS_LOCALHOST_ONLY=0
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
```

在仓库根目录启动第一阶段的 A 席位程序：

```bash
./run_contestant.sh A 1
```

启动第二阶段的 A 席位程序：

```bash
export RMTC_TEAM_TOKEN=practice-A
export AI_GATEWAY=http://127.0.0.1:8080/v1/chat/completions
./run_contestant.sh A 2
```

脚本的两个位置参数分别为 **席位 A/B/C/D** 和 **阶段 1/2**。第一阶段使用根安装目录，第二阶段转入 `run_stage2_final.sh`，直接运行独立安装程序并加载 `stage2/config/final.yaml`。默认总 Tick 分别为100、500；二阶段尚未构建时明确报错，不回退到根目录旧程序。

第二阶段换席位时，需要同时更换令牌。例如 B 席位在练习中使用 `practice-B`。同一席位只能由一个选手程序或参考车控制。

更多配置通过 ROS 参数传入：

```bash
./run_contestant.sh A 2 -p team_name:=我的队伍 -p ai_budget:=90
# 自定义练习长度时，显式修改总 Tick
./run_contestant.sh A 2 -p max_ticks:=200
```

也可直接使用二阶段发布入口；后置的显式 ROS 参数仍可覆盖发布配置：

```bash
./run_stage2_final.sh A
# 仅用于复现B0对照，最终发布默认启用阻塞恢复
./run_stage2_final.sh A -p resource_blocked_recovery:=false
```

## 正式比赛启动流程

正式比赛使用 `run_contestant.sh` 连接主办方裁判和 AI 网关。`run_deepseek.sh` 用于本地练习；正式 AI 调用需要经过主办方网关，才能计入本场席位的审计记录。

### 1. 赛前准备

代码封存前完成构建与测试。原生 Ubuntu 成员进入本仓库根目录，使用 Bash 终端；Docker 成员先进入自己的开发容器，再进入容器内挂载的仓库根目录。维护者现有 Docker 环境示例：

```bash
docker exec -it ros2_humble_gazebo bash
cd /root/ros2_ws
```

在仓库根目录构建：

```bash
./build_contestant.sh
```

上场前连接比赛有线网络，并取得主办方公布的本场席位、`ROS_DOMAIN_ID`、AI 网关完整请求地址、本场席位令牌、模型名称和输出 token 上限。确认本阶段总 Tick 是否调整。代码封存后按赛事要求操作，现场配置以主办方通知为准。

### 2. 设置本场通信和 AI 配置

在**实际运行选手程序的终端**执行以下 Bash 命令。`27` 只是 ROS 域示例，网关地址中的占位符也必须替换；若使用 Docker，应在容器内设置这些变量。

```bash
# 27 只是示例，必须按现场公布的 ROS 域修改
export ROS_DOMAIN_ID=27
export ROS_LOCALHOST_ONLY=0
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp

# 正式比赛使用主办方网关
export RMTC_AI_PROVIDER=gateway
export AI_GATEWAY='http://<主办方网关IP>:<端口>/v1/chat/completions'

# 未指定模型时，使用主办方网关的默认模型
unset RMTC_AI_MODEL
```

如果主办方明确要求模型名称，再执行：

```bash
export RMTC_AI_MODEL='主办方指定的模型名称'
```

第二阶段在同一终端隐藏输入本场席位令牌：

```bash
read -r -s -p '请输入本场席位令牌：' RMTC_TEAM_TOKEN
echo
export RMTC_TEAM_TOKEN
```

第一阶段不调用 AI，可跳过模型和令牌设置。维护者现有容器原本默认 `ROS_DOMAIN_ID=0`，因此每次进入新的比赛终端都要显式设置现场公布的域。正式比赛令牌使用主办方发放的本场令牌，密钥和令牌不写进 README、参数文件或仓库。

### 3. 启动对应阶段和席位

第一阶段按规则使用 A 席位：

```bash
./run_contestant.sh A 1 -p team_name:=你的队名
```

第二阶段按主办方分配的席位启动，例如分配到 B：

```bash
./run_contestant.sh B 2 -p team_name:=你的队名
```

脚本两个位置参数分别是**席位、阶段**，同一席位只启动一个选手进程。默认第一阶段 100 Tick，第二阶段 500 Tick。若现场要求调整总 Tick 或 AI 输出上限，显式覆盖，以下数值仍为示例：

```bash
./run_contestant.sh B 2 \
  -p team_name:=你的队名 \
  -p max_ticks:=500 \
  -p ai_max_tokens:=2500
```

### 4. 等待开始并确认接入

第二阶段看到 `C++ contestant ready: team=B stage=2 AI=async` 表示节点已启动。随后确认持续收到本队感知并产生动作，后续感知中的 `self.last_action` 有对应回执，并由裁判确认本场席位的真实 AI 调用资格。节点本地的 HTTP 成功日志不能代替裁判审计。

没有感知时，先检查席位、ROS 域及 DDS 网络；AI 返回 `401` 时，先检查本场席位令牌。网页或 AI 网关可访问，并不表示 ROS 话题已经双向连通。比赛开始后让程序自主运行，换场、重启或调整配置按主办方许可和赛事规则执行。

当前已通过本地裁判联调和 DeepSeek 官方真实接口测试，尚未与主办方正式网关联调。应在代码封存前的官方联调中完整验证模型返回结构、输出限制和本场审计资格。

## 与练习裁判联调

练习裁判由主办方单独提供，不包含在本仓库中。解压后按其说明准备环境。下面假设其目录为 `~/rmtc_practice`，请按自己的实际位置调整。

### 第一阶段

终端 1 运行裁判：

```bash
export ROS_DOMAIN_ID=27
export ROS_LOCALHOST_ONLY=0
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
cd ~/rmtc_practice
./start_referee.sh --stage 1
```

终端 2 回到**本仓库根目录**，运行选手：

```bash
export ROS_DOMAIN_ID=27
./run_contestant.sh A 1
```

在裁判所在机器的浏览器打开 [练习控制台](http://127.0.0.1:8080)，待程序接入后点击“开始 / 继续”。通过动作诊断和后续感知中的 `self.last_action` 确认双向通信。

### 第一阶段默认算法（e075）

当前阶段一默认使用 e075：在 `stage1_subgoal.cpp` 中把侦察候选排序的距离指数从 `0.85` 调为 `0.75`，即更偏好近处、可快速兑现的视野收益，减少长距离侦察。该改动很小：在 139 局真实裁判对照中仅 6 张图的决策发生变化，其余逐图不变；合并均值 +0.13、满分 41→43，唯一退步为 1 张 −2。

回退旧行为：

```bash
./run_contestant.sh A 1 -p stage1_resource_route:=false \
  -p stage1_joint_coverage:=false -p stage1_subgoal_risk:=false
```

也可以只关闭某一项做消融；三个参数默认 `true` 的目的是让正式启动无需额外参数即使用已验证算法。规划层仍只使用本队感知与公开公告，不读取练习配置或裁判全图。

完整验证数据、逐图结果与复现命令见 [阶段一 e075 验证摘要](docs/STAGE1_E075_VALIDATION.md)。

### 第二阶段

先停止上一场裁判和选手，再在裁判目录启动：

```bash
./start_referee.sh --stage 2 --bots B,C,D
```

此命令延用前面裁判终端的 ROS 环境。A 席位留给本程序，B/C/D 由陪练控制。选手终端在仓库根目录执行：

```bash
export ROS_DOMAIN_ID=27
export RMTC_TEAM_TOKEN=practice-A
export AI_GATEWAY=http://127.0.0.1:8080/v1/chat/completions
./run_contestant.sh A 2
```

裁判默认暂停，需要在网页点击开始。第一阶段默认 `100 ms × 100 Tick`，第二阶段默认 `500 ms × 500 Tick`。

### 异机运行

裁判和选手分处两台设备时，两端需要相同的 ROS 域、兼容的 DDS 配置和可双向通信的局域网。`127.0.0.1` 指当前运行环境自身，不能直接用它访问另一台机器的裁判。

练习裁判的网页和 AI 代理只监听裁判机回环地址。可在**选手机**建立 SSH 转发，将用户名和地址替换为实际值：

```bash
ssh -N -L 18080:127.0.0.1:8080 user@referee-host
```

保持转发终端运行，选手机浏览器访问 [转发后的控制台](http://127.0.0.1:18080)，选手程序设置：

```bash
export AI_GATEWAY=http://127.0.0.1:18080/v1/chat/completions
```

SSH 转发只处理网页和 AI HTTP 请求；ROS 话题仍依赖局域网中的 DDS 通信。

## LLM 配置

练习裁判默认使用 `mock` 模式，只验证 HTTP 链路。固定回复可能不符合事件结构，程序会拒绝该内容并继续本地控制，不能据此判断真实事件理解能力。

要验证真实模型，按练习裁判教程设置 `PRACTICE_LLM_URL`、`PRACTICE_LLM_KEY` 和 `PRACTICE_MODEL`，并以 `--ai-mode real` 启动裁判。选手程序仍访问练习裁判的本地代理，使用与席位对应的 `practice-*` 令牌。

正式比赛使用主办方发放的**本场席位、ROS 域、网关、模型和令牌**。程序不会自动填入练习令牌；令牌通过环境变量或私密文件提供，不提交到代码仓库。正式任务计分资格由裁判的真实调用记录确认。

### 本地直接使用 DeepSeek 官方接口（关闭思考）

构建后，在能够访问互联网的选手终端执行：

```bash
export ROS_DOMAIN_ID=27
./run_deepseek.sh A 2
```

脚本会隐藏输入并询问 DeepSeek API key；也可提前设置 `DEEPSEEK_API_KEY`。它使用 `https://api.deepseek.com/chat/completions`、`deepseek-flash`，显式发送 `thinking: {type: disabled}`、JSON 输出模式和 `temperature: 0`。请求保持非流式，连接在工作线程内复用，逐 Tick 控制继续独立运行。可通过 `RMTC_AI_MODEL` 调整模型。

此方式只把 AI 请求发往 DeepSeek，ROS 感知和动作仍连接本地练习裁判。使用真实服务会产生 API 费用。直连请求不会进入裁判的 AI 审计记录，**不能用它取得正式比赛的 AI 调用资格**。正式上场应在终端设置 `RMTC_AI_PROVIDER=gateway`、主办方的 `AI_GATEWAY`、`RMTC_AI_MODEL` 和 `RMTC_TEAM_TOKEN`，使用 `run_contestant.sh`。

若宿主机通过代理访问互联网，容器也需要相应网络环境。可以在宿主机进入容器时传入现有变量，不要把具体代理地址写死在仓库：

```bash
docker exec -it -e HTTPS_PROXY -e https_proxy -e HTTP_PROXY -e http_proxy \
  -e ALL_PROXY -e all_proxy -e NO_PROXY -e no_proxy ros2_humble_gazebo bash
```

容器名按自己的环境替换；代理回环地址只有在容器使用 host 网络等可达配置时才可访问。此次维护者环境的容器直连曾在 DNS 阶段超时，继承宿主机现有代理后通过实测。

独立测速工具同样使用项目的 C++ `AiClient`、完整提示词和结构校验，不需要裁判，也不自动加入付费调用到单元测试中：

```bash
cmake --build build/rmtc_bot --target ai_benchmark -j 4
read -r -s -p 'DeepSeek API key: ' DEEPSEEK_API_KEY
echo
export DEEPSEEK_API_KEY
./build/rmtc_bot/ai_benchmark --live-deepseek
# 可选：合成 ROS 感知，验证真实 AI 请求期间的连续动作回包
source /opt/ros/humble/setup.bash
python3 tools/deepseek_wire_test.py --live-deepseek
unset DEEPSEEK_API_KEY
```

独立测速包含 12 个请求样例；ROS 测试使用隔离域 46、120 个 100 ms Tick、本地预算 4。均会调用真实付费 API。2026-10-21 的 12 样例响应均无思考内容，平均完整响应 1.51 秒；ROS 闭环动作往返 P95 1.40 ms。样例通过情况和测量局限见 [VALIDATION.md](VALIDATION.md)。接口参数依据 [DeepSeek 官方思考模式文档](https://api-docs.deepseek.com/zh-cn/guides/thinking_mode/) 和 [Chat Completions 文档](https://api-docs.deepseek.com/zh-cn/api/create-chat-completion/)。

| 参数 | 默认值 / 用途 |
| --- | --- |
| `team` / `team_name` | `A` / `图灵探索者`，队名为 1–96 个 UTF-8 字节 |
| `stage` / `max_ticks` | 直接启动默认 `2` / `500`；脚本第一阶段设置为 `1` / `100` |
| `ai_provider` | `RMTC_AI_PROVIDER` 或 `gateway`；`deepseek` 模式关闭思考并启用 JSON 输出 |
| `ai_gateway` | `AI_GATEWAY` 环境变量，未设置时使用本机 8080 的完整 completions 地址 |
| `ai_model` | `RMTC_AI_MODEL` 环境变量；网关模式空值使用网关默认模型，DeepSeek 模式默认 `deepseek-flash` |
| `ai_enabled` | `true`；第一阶段不调用 AI |
| `ai_timeout_ms` | `12000`，请求在独立线程中执行 |
| `ai_max_tokens` | `2500`，需符合服务方输出限制 |
| `ai_budget` | `90`，本地成功调用预算；`0` 为不限制，网关配额仍然生效 |
| `stage1_resource_route` | `true`；第一阶段启用 e075 规划器（默认） |
| `stage1_joint_coverage` | `true`；第一阶段联合资源-视野评估（默认） |
| `stage1_subgoal_risk` | `true`；第一阶段子目标/路线评估（默认） |
| `token_file` | 可选私密令牌文件，换场时读取；默认在网关模式读 `RMTC_TEAM_TOKEN`，DeepSeek 模式读 `DEEPSEEK_API_KEY` |
| `log_dir` | `RMTC_LOG_DIR`，未设置时使用 `~/.rmtc-bot` |

参数文件示例位于 [config/contestant.yaml](src/rmtc_bot/config/contestant.yaml)。使用时注意它自身包含席位、阶段和总 Tick，确保与当前场次一致；后面的同名参数可覆盖前面的配置：

```bash
./run_contestant.sh B 2 \
  --params-file src/rmtc_bot/config/contestant.yaml \
  -p team:=B -p stage:=2 -p max_ticks:=500
```

## 目录与协作分工

```text
src/rmtc_bot/
├── include/rmtc_bot/        # 核心类型、规划配置与 AI 客户端接口
├── src/node.cpp             # ROS 通信、场次管理、参数、动作提交和日志
├── src/core.cpp             # 感知解析、地图记忆、路径与第二阶段决策
├── src/stage1_planner.cpp   # 第一阶段基础规划（资源/探索/路径）
├── src/stage1_joint.cpp     # 第一阶段联合资源-视野巡逻（实验开关）
├── src/stage1_subgoal.cpp   # 第一阶段子目标/路线评估（e075 改动位置）
├── src/ai.cpp               # 异步 HTTP、模型输出解析、预算和失败处理
├── config/                  # 参数配置
├── test/                    # C++ 单元测试（core_test 140 项 + ai_test 14 项）
└── tools/                   # ai_benchmark / event_revision_benchmark / stage1_* 开发工具
tools/                       # 维护者环境中的 Python 联调与审计脚本
stage2/src/rmtc_bot/         # 完整冻结二阶段源码（与根一阶段包隔离）
stage2/config/final.yaml     # 二阶段B1发布参数
stage2/SOURCE_MANIFEST.json  # 发布源码和参数校验值
build_contestant.sh          # 依次构建两阶段
build_stage2.sh              # 只构建二阶段
run_contestant.sh            # 按阶段选择安装程序
run_stage2_final.sh          # 二阶段发布入口
run_deepseek.sh              # 本地直连 DeepSeek 官方、关闭思考的运行入口
```

第二阶段默认启用前期资源/探索过程中的有限脱离交火收益（`productive_escape=true`），可通过 `-p productive_escape:=false` 对照旧行为。其跨地图收益与退步案例见 [VALIDATION.md](VALIDATION.md)，实验开关说明见 [docs/STRATEGY.md](docs/STRATEGY.md)。

已迁移到 v1.1 裁判规则：对抗项500点满分，死亡扣60，致死批次合格贡献者平分60奖励。感知中的 `effective_damage` 是调整后权威值，可下降；程序直接采用，不重复加减。规则和新版验证方法见 [docs/RULES_V1_1.md](docs/RULES_V1_1.md)。

规划层逐 Tick 执行动作，LLM 异步理解公开随机事件。随机事件原文不通过正则或关键词替代模型理解；模型结果经过校验后才进入本地事件状态。程序使用本队感知和公开公告，不以裁判全图或内部接口作为决策输入。

模型输出被事件结构校验拒绝时，可交回 LLM 进行一次有界修复：每条公告至多一次、每场至多八次，计入原 `ai_budget`，新公告优先。修复请求仅携带原公开输入、错误分类、上次模型输出和已验证事件状态，仍须通过完整校验；日志记录 `schema_error`、`repair_attempt` 和 `repair_queued`，不保存凭据或任意异常文本。

可显式运行带故障注入的真实模型闭环测试（会产生付费 API 调用）：

```bash
python3 tools/deepseek_wire_test.py --live-deepseek --schema-repair-fault < /private/path/test-key
```

该测试只发布合成的本队公开感知，不启动裁判或读取全图。它人为返回一次缺少事件点位的响应，再让真实 DeepSeek 修复；正常情况下是三次真实上游调用加一次注入响应，全部计入节点设置的四次成功调用预算。200 个 100 ms Tick 用于检查动作连续性和事件状态，不能作为比赛得分或正式审计资格的证明。

## 测试与日志

在仓库根目录构建后执行：

```bash
source /opt/ros/humble/setup.bash
source install/setup.bash
colcon test --base-paths src --packages-select rmtc_bot
colcon test-result --verbose
```

独立二阶段发布包的检查：

```bash
source /opt/ros/humble/setup.bash
colcon --log-base stage2/log test --base-paths stage2/src --build-base stage2/build --packages-select rmtc_bot
colcon test-result --test-result-base stage2/build --verbose
python3 tools/verify_stage2_release.py
```

当前已通过的场次、成绩、决策耗时和验证范围见 [VALIDATION.md](VALIDATION.md)。已完成 DeepSeek 非思考的有限事件样例、合成 ROS 闭环，以及本地练习裁判的完整真实模型对抗；正式 AI 审计仍待验证，跨地图稳定第一尚未达标。

跨地图策略优化、单车对三车的输入边界和可复现对照方法见 [第二阶段策略说明](docs/STRATEGY.md)。新增评估工具会冻结选手二进制，分别轮换席位，并支持修改地图尺寸、障碍、资源和任务坐标；真实模型与无模型对照按正式节奏运行。最新完整对抗结果以 `VALIDATION.md` 的后续记录为准，不以单场成绩保证稳定第一。

`tools/integration.py` 和 `tools/wire_test.py` 目前固定使用维护者的容器路径，**不能作为任意原生环境下的通用入口**。其运行条件和命令保留在 [本地开发记录](docs/DEVELOPMENT_NOTES.md)；原生环境可以先按上面的双终端流程联调。

运行日志为 JSONL，记录动作、回执、得分、任务和 AI 请求结果，以及模型、token 用量、思考内容长度，不记录密钥、HTTP 请求头或思考原文。`build/`、`install/`、`log/` 和 `test-results/` 已加入 Git 忽略列表。

阶段一 e075 的逐图真实裁判数据（19 张 20×20 开发图 + 4 组各 30 张独立图）、离线反事实工具与安装验证记录，摘要见 [阶段一 e075 验证摘要](docs/STAGE1_E075_VALIDATION.md)，原始数据存档于仓库外维护目录 `../analysis_opt_order_20261025/`；当前包的单元测试为 `core_test` 140 项、`ai_test` 14 项。

## 可选：Docker 和 IDE

Docker 使用者只需进入已配置好依赖、挂载本仓库的容器，然后按同样的构建和运行步骤操作。以下是**维护者现有环境示例**，其他成员按自己的容器名和挂载目录调整：

```bash
docker exec -it ros2_humble_gazebo bash
cd /root/ros2_ws
```

跨容器或主机通信时，还需确认网络可达性及实际动作回执。维护者当前容器的路径映射和网络细节见 [本地开发记录](docs/DEVELOPMENT_NOTES.md)。

CLion 可打开 `src/rmtc_bot` 作为 CMake 项目。其他支持编译数据库的编辑器可使用 `build/rmtc_bot/compile_commands.json`。构建、索引和调试进程都应加载对应的 ROS Humble 环境。

四车独立测试的运行方式、合法输入隔离与轮换方法见 [SELFPLAY.md](docs/SELFPLAY.md)。正式参赛仍只启动本队一个席位。

当前默认已启用经四车配对验证的追击循环修正与低血量联合攻防估值，资源竞争、固定开火概率与坐标任务优先实验仍关闭。`run_contestant.sh`加载该默认策略；开发对照可显式切换参数，结果和限制见[策略说明](docs/STRATEGY.md)与[验证记录](VALIDATION.md)。

死亡后的移动可执行性、复活返程与任务出发时机复盘见[死亡恢复记录](docs/DEATH_RECOVERY_REVIEW.md)。相关实验目前默认关闭；日志中的存活、保护、公开任务和初始出生点只用于本车合法历史复盘。

低血量推进的下一Tick火线实验及完整四臂反例见[动态火线记录](docs/DYNAMIC_FIRE_REVIEW.md)。`safe_progress_moves`默认关闭，组合任务路线未提高最终得分；当前默认事件解释已用同一安装版本完成双真实调用对照，定向幻术案例32.5→57.5分、第三→第一，验证范围见该记录。

历轮实验的选择、反例和后续方向见[综合复盘](docs/EXPERIMENT_SYNTHESIS.md)。堡垒时间预算已拆成独立实验`fortress_time_budget`，默认关闭；事件测试除分数外，还需核对模型解释、返回时间及对手是否先出现行动差异。历史模型增分归属各次冻结版本，不能自动沿用到后续构建。

按新的复查方向，原阶段目标已[归档暂停](docs/GOAL_ARCHIVE_2026-10-23.md)，逐项判断与实施结果见[新方向记录](docs/NEW_IDEA_IMPLEMENTATION.md)。首项`event_revisions`用于模型明确引用既有事件并修正其解释，仍默认关闭；本地及真实合成样例通过，但完整对照尚无增分，部分反馈迟到。正式入口继续只控制本席位。

第二项`purity_joint_actions`已实现单敌攻击/防御互斥估值，默认关闭。130项检查通过；四图真实提前反馈对照触发了估值，但行动、比分与排名均未改变，暂不推广。具体适用范围与完整证据仍见[新方向记录](docs/NEW_IDEA_IMPLEMENTATION.md)。


## 变更记录

- **2026-10-26**：按当前实现更新 README：补充“当前版本状态”、阶段一 e075 默认算法与回退方式、参数表阶段一默认项、目录结构、验证与测试说明。
- **2026-10-25**：正式包阶段一默认算法切换为 e075（`node.cpp` 中 `stage1_resource_route` / `stage1_joint_coverage` / `stage1_subgoal_risk` 默认值改为 `true`）；真实裁判验证：139 局合并 +0.1295、满分 41→43、1 张 −2；同机低负载 P95 ≤23.7 ms。改动前备份：`contestant_ws/.backup-rmtc-bot-e075-defaults-false-20261025-214900/`。
- **2026-10-25**：正式包源码更新为 v4-order-v2 实验分支 + e075 单行改动，阶段一逻辑拆分为 `stage1_planner.cpp` / `stage1_joint.cpp` / `stage1_subgoal.cpp`；第二阶段默认决策路径等价于原包（AI 客户端逐字节一致）。
