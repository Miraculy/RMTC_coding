# RMTC C++ 参赛程序

基于 C++17、ROS 2 Humble 的自主参赛程序，支持第一阶段资源采集，以及第二阶段资源争夺、公开任务、对抗和异步 LLM 事件理解。

本 README 按《RMTC机甲大师图灵对决赛参赛手册》“二、赛前提交什么—1. 代码仓库”的要求说明目录、环境、构建、启动、接入参数和运行逻辑。比赛参数以适用本场的正式书面通知为准。更新日期：2026-10-31。

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

当前未修复的实现边界：

- 二阶段话题和感知路径都只向 LLM 提交含“预告”的公告；手册与协议不保证固定句式，可能漏掉合法公告或补充信息，存在兼容与合规解释风险。
- 感知读取结构化 `missions`，未读取 `mission_text`；任务话题文本换场未清空，资格请求可能短暂使用空或旧文本。伤害任务没有单独计算达标奖励。
- 洁身防御未计反伤致死分奖；辣椒与堡垒最后有效驻留窗口的估值仍不完整。权威得分始终由裁判结算。
- 一阶段优化内置标准资源数量、价值和200R总量假设，调整资源配置时未完整参数化。
- 缺令牌时仍继续本地控制；模型默认输出上限2500，正式接入须按第5节覆盖为512。模型结果被截断时会拒绝该解释并继续本地策略，当前未声称512限制下已完成真实模型验证。

本 README 更新仅整理仓库运行说明，不改变算法、Prompt、配置或程序实现。
