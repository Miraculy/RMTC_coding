# RMTC C++ 参赛程序

基于 **C++17 和 ROS 2 Humble** 的自主参赛程序，支持资源采集、任务规划、基础对抗，以及通过官方 LLM 网关理解随机事件。

**可以直接在 Ubuntu 上开发和运行，Docker 为可选环境。** 本仓库本身就是一个 ROS 2 工作空间，以下未特别说明的命令均在仓库根目录执行，目录名称和存放位置可以自行选择。

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

脚本会自动加载 ROS Humble 环境，并用 colcon 构建 `rmtc_bot`。也可以手工构建：

```bash
source /opt/ros/humble/setup.bash
colcon build --symlink-install --packages-select rmtc_bot \
  --cmake-args -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

生成目录为 `build/`、`install/` 和 `log/`。换机器或更改工作空间位置后，重新构建，不要直接复用其他成员的生成目录。

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

脚本的两个位置参数分别为 **席位 A/B/C/D** 和 **阶段 1/2**。它会自动加载本工作空间的安装环境，并设置对应的默认总 Tick：第一阶段 100，第二阶段 500。

第二阶段换席位时，需要同时更换令牌。例如 B 席位在练习中使用 `practice-B`。同一席位只能由一个选手程序或参考车控制。

更多配置通过 ROS 参数传入：

```bash
./run_contestant.sh A 2 -p team_name:=我的队伍 -p ai_budget:=90
# 自定义练习长度时，显式修改总 Tick
./run_contestant.sh A 2 -p max_ticks:=200
```

也可在加载环境后直接运行 ROS 可执行程序：

```bash
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 run rmtc_bot contestant --ros-args -p team:=A -p stage:=2
```

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

| 参数 | 默认值 / 用途 |
| --- | --- |
| `team` / `team_name` | `A` / `图灵探索者`，队名为 1–96 个 UTF-8 字节 |
| `stage` / `max_ticks` | 直接启动默认 `2` / `500`；脚本第一阶段设置为 `1` / `100` |
| `ai_gateway` | `AI_GATEWAY` 环境变量，未设置时使用本机 8080 的完整 completions 地址 |
| `ai_model` | `RMTC_AI_MODEL` 环境变量；空值使用网关默认模型 |
| `ai_enabled` | `true`；第一阶段不调用 AI |
| `ai_timeout_ms` | `12000`，请求在独立线程中执行 |
| `ai_max_tokens` | `2500`，需符合服务方输出限制 |
| `ai_budget` | `90`，本地成功调用预算；`0` 为不限制，网关配额仍然生效 |
| `token_file` | 可选私密令牌文件，换场时读取；默认读 `RMTC_TEAM_TOKEN` |
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
├── include/rmtc_bot/    # 核心类型、规划与 AI 客户端接口
├── src/node.cpp        # ROS 通信、场次管理、动作提交和日志
├── src/core.cpp        # 感知解析、地图记忆、目标选择、路径和战斗评估
├── src/ai.cpp          # 异步 HTTP、模型输出解析、预算和失败处理
├── config/             # 参数配置
└── test/               # C++ 单元测试
tools/                  # 维护者环境中的开发联调脚本
build_contestant.sh      # 构建入口
run_contestant.sh        # 运行入口
```

规划层逐 Tick 执行动作，LLM 异步理解公开随机事件。随机事件原文不通过正则或关键词替代模型理解；模型结果经过校验后才进入本地事件状态。程序使用本队感知和公开公告，不以裁判全图或内部接口作为决策输入。

## 测试与日志

在仓库根目录构建后执行：

```bash
source /opt/ros/humble/setup.bash
source install/setup.bash
colcon test --packages-select rmtc_bot
colcon test-result --verbose
```

当前已通过的场次、成绩、决策耗时和验证范围见 [VALIDATION.md](VALIDATION.md)。真实 LLM 理解准确率尚未验证，当前对抗策略属于启发式基线。

`tools/integration.py` 和 `tools/wire_test.py` 目前固定使用维护者的容器路径，**不能作为任意原生环境下的通用入口**。其运行条件和命令保留在 [本地开发记录](docs/DEVELOPMENT_NOTES.md)；原生环境可以先按上面的双终端流程联调。

运行日志为 JSONL，记录动作、回执、得分、任务和 AI 请求结果，不记录令牌或 HTTP 请求头。`build/`、`install/`、`log/` 和 `test-results/` 已加入 Git 忽略列表。

## 可选：Docker 和 IDE

Docker 使用者只需进入已配置好依赖、挂载本仓库的容器，然后按同样的构建和运行步骤操作。以下是**维护者现有环境示例**，其他成员按自己的容器名和挂载目录调整：

```bash
docker exec -it ros2_humble_gazebo bash
cd /root/ros2_ws
```

跨容器或主机通信时，还需确认网络可达性及实际动作回执。维护者当前容器的路径映射和网络细节见 [本地开发记录](docs/DEVELOPMENT_NOTES.md)。

CLion 可打开 `src/rmtc_bot` 作为 CMake 项目。其他支持编译数据库的编辑器可使用 `build/rmtc_bot/compile_commands.json`。构建、索引和调试进程都应加载对应的 ROS Humble 环境。
