# 本地开发上下文与原 README 备份

记录日期：2026-10-21。本文件用于保留维护者环境和后续开发所需的上下文，不作为所有协作成员的环境要求。

## 后续维护时需要记住

- 当前安装051252...，80项汇总测试通过；事件解释默认开启，多步避险和共同损失实验默认关闭。双真实调用压力场+7.5分已复现，并验证实际默认无需新开关也有同样收益；未见图仍无增分。全20场10000帧完成，不能混合四版本第一率。下一步和边界见VALIDATION.md末尾、docs/TICK_REVIEW.md。

- 最新前期脱离交火默认推广完成70项汇总检查和500Tick无覆盖实战等价检查；冻结24对第一13→15，保留退步。真实标准强度模型新场所有事件及时返回仍未增分。以VALIDATION.md末尾记录为准。

- 本轮单车对三车、跨地图策略优化及评估入口见 `docs/STRATEGY.md`。新工具 `tools/strategy_eval.py` 支持路径参数、独立域/端口和冻结二进制；裁判真值仅供测试驱动评估，不回传选手。最新默认动态占用路径已完成 62 项汇总检查；同一冻结程序 24 对中仅一场 +2.5 分、第一数不变，正常模型新场未增分。最新实测在 `VALIDATION.md` 后续记录，不能沿用旧基线或单张地图的胜率作为当前版本结论。

- 参赛程序必须使用 C++17，运行于 ROS 2 Humble；Python 只用于开发测试脚本。
- 协作成员可能使用原生 Ubuntu，项目主 README 必须以通用工作空间为基础，Docker 是可选方式。
- 维护者宿主机工作空间是 `/home/miraculy/docker/ros2/contestant_ws`，同级 `ros2_ws` 是指向它的符号链接。
- 当前开发容器名为 `ros2_humble_gazebo`，容器内工作空间是 `/root/ros2_ws`，练习裁判是 `/root/rmtc_practice`。这些路径不能推广为所有成员的默认路径。
- 外层 Docker Compose 使用 host 网络，原有 `ROS_DOMAIN_ID=0`；练习时裁判和选手终端均显式设置为 27。
- C++ 包是 `rmtc_bot`，可执行程序是 `contestant`。构建入口为 `build_contestant.sh`，启动入口为 `run_contestant.sh`。
- `tools/integration.py` 固定使用 `/root/ros2_ws`、`/root/rmtc_practice`、公共域 40、内部域 41、HTTP 8081；`tools/wire_test.py` 固定使用 `/root/ros2_ws` 和公共域 44。尚未完成通用路径参数化。
- 初始基线通过 29 个 GTest；最新参数化、探索、竞争和短路线搜索实验通过 60 个 GTest（另有 2 个 CTest 容器，共 62 项）。此前完成公开 ROS 通信恢复、第一阶段和完整第二阶段集成检查。不同冻结二进制的实测需分开归属；具体成绩、耗时与局限以 `VALIDATION.md` 为准。
- 前期完整裁判场次使用 mock；本轮另完成了 DeepSeek 官方 Flash 非思考的完整单车对三车配对对抗。真实模型在部分事件压力场已有增分证据，仍不能据此保证广泛公告准确率、跨地图稳定第一或正式 AI 审计资格。
- 本地直连入口 `run_deepseek.sh`，密钥通过 `DEEPSEEK_API_KEY` 或隐藏输入提供。容器直连外网 DNS 曾超时；继承宿主机现有代理变量后实测通过。不要写死具体代理地址，也不要将本次临时密钥保存到文档。正式比赛继续使用 `gateway` 模式和主办方网关。
- 实现是启发式基线，后续重点是资源竞争和战术优化；不应把默认练习地图成绩表述为正式比赛保证。

## 改写前的 README 全文

下面原样保留本次改写前的内容，以便查阅当时的启动命令、参数及环境细节。

---

# RMTC C++ 参赛程序

运行环境为现有 Docker 容器 `ros2_humble_gazebo`（Ubuntu 22.04 / ROS 2 Humble）。宿主机目录 `contestant_ws` 映射为容器内 `/root/ros2_ws`。参赛节点、规划、事件状态管理和 HTTP 客户端均为 C++17；`tools/integration.py` 仅用于开发时启动裁判和核对成绩，不参与参赛决策。

## 构建

在宿主机进入开发容器：

```bash
docker exec -it ros2_humble_gazebo bash
cd /root/ros2_ws
./build_contestant.sh
```

依赖：`rclcpp`、`std_msgs`、`ament_cmake`、`nlohmann-json3-dev`、`libcurl4-openssl-dev`，测试使用 `ament_cmake_gtest`。当前开发容器均已具备。换容器时可安装：

```bash
apt-get update
apt-get install -y ros-humble-rclcpp ros-humble-std-msgs ros-humble-ament-cmake-gtest nlohmann-json3-dev libcurl4-openssl-dev
```

## 第一阶段

裁判终端：

```bash
export ROS_DOMAIN_ID=27 ROS_LOCALHOST_ONLY=0 RMW_IMPLEMENTATION=rmw_fastrtps_cpp
cd /root/rmtc_practice
./start_referee.sh --stage 1
```

选手终端（同一容器的另一个 shell）：

```bash
cd /root/ros2_ws
export ROS_DOMAIN_ID=27
./run_contestant.sh A 1
```

浏览器打开 `http://127.0.0.1:8080`，等待节点启动后点击“开始 / 继续”。不要同时启动控制 A 的参考车。

## 第二阶段

裁判终端：

```bash
export ROS_DOMAIN_ID=27 ROS_LOCALHOST_ONLY=0 RMW_IMPLEMENTATION=rmw_fastrtps_cpp
cd /root/rmtc_practice
./start_referee.sh --stage 2 --bots B,C,D
```

选手终端：

```bash
cd /root/ros2_ws
export ROS_DOMAIN_ID=27
export RMTC_TEAM_TOKEN=practice-A
export AI_GATEWAY=http://127.0.0.1:8080/v1/chat/completions
./run_contestant.sh A 2
```

默认模拟 AI 只验证 HTTP 链路，固定回复可能不符合事件结构，节点会拒绝它并继续本地控制。这不代表模型理解事件已经验证。真实事件理解需要按练习裁判教程配置 `PRACTICE_LLM_URL`、`PRACTICE_LLM_KEY`、`PRACTICE_MODEL`，用 `--ai-mode real` 启动裁判；选手仍访问本地代理并携带 `practice-A`。本程序不读取这些上游密钥。

正式上场时，替换为本场席位、ROS 域、统一网关、模型和令牌。脚本不会默认填入练习令牌。注意 Compose 中原有 ROS_DOMAIN_ID=0，启动裁判和选手的终端都要显式改成相同的域。

更多参数通过 ROS 参数传入，例如：

```bash
./run_contestant.sh B 2 --params-file src/rmtc_bot/config/contestant.yaml -p team:=B -p team_name:=我的队伍 -p ai_budget:=90
```

示例参数文件中的 team/stage/max_ticks 会覆盖脚本前面传入的同名参数；使用文件时确保与场次一致。也可直接使用 `ros2 run rmtc_bot contestant --ros-args ...`。自定义练习总 Tick 时必须设置 `max_ticks`；感知没有阶段或总 Tick 字段，程序不会凭地图猜测。

| 参数 | 默认值 / 用途 |
| --- | --- |
| team / team_name | A / 图灵探索者，队名 1–96 UTF-8 字节 |
| stage / max_ticks | 2 / 500；脚本第一阶段设置 1 / 100 |
| ai_gateway | AI_GATEWAY 环境变量或本地 8080 完整 completions 地址 |
| ai_model | RMTC_AI_MODEL 环境变量；空值使用网关默认模型 |
| ai_enabled | true，第一阶段始终不调用 AI |
| ai_timeout_ms | 12000，独立工作线程的请求超时，不阻塞逐 Tick 控制 |
| ai_max_tokens | 2500，按主办方公布的限制调整 |
| ai_budget | 90，本地成功调用预算，0 为不限制；429 quota_exceeded 仍会停止调用 |
| token_file | 可选私密文件，换场时读取；默认读 RMTC_TEAM_TOKEN |
| log_dir | RMTC_LOG_DIR 或 ~/.rmtc-bot |

## 实现结构

- `src/rmtc_bot/src/node.cpp`：公开话题及 QoS、注册、场次/Tick 去重、回执日志、动作提交。
- `src/rmtc_bot/src/core.cpp`：感知解析、相对坐标转换、局部地图记忆、资源与任务选点、Dijkstra 路径、即时攻击/防御评分、八种事件的本地执行机制。
- `src/rmtc_bot/src/ai.cpp`：单工作线程、libcurl 非流式请求、去重、限次、退避、模型 JSON 解析和跨场次隔离。

节点只订阅本队感知、公开任务、事件和队伍名录，发布本队动作与注册。不会读取裁判配置、内部话题、全图接口或裁判日志用于决策。随机事件原文交给 LLM，节点仅校验其结构化输出；不存在关键词/正则事件解析降级。

规划根据实际地图尺寸和合法感知建立记忆，未知格带额外路径成本。每帧重新选择资源、未完成任务、探索点或事件目标，并仅执行下一步。战斗按移动前位置评分，支持防御、保护、无双范围攻击和易伤；目标动作存在不确定性，以风险估计处理，不能准确预知敌人下一步。

事件以 `[start_tick,end_tick)` 保存。幻术按激活时的位置历史判断，成功移动回执用于回血/易伤触发，无双按连续帧驻留推断，效果不因阵亡清空。漏帧导致证据不足时不伪造触发记录；模型提供的明确授予公告可补充效果。公告去重先按原文，再按模型输出的事件种类/激活 Tick 合并。同种同 Tick 的不同区域事件按几何位置分别保存。

对尚未理解的事件继续本地采集/战斗，无法保证规避未知事件。当前是启发式策略，未做强化学习或多步对手博弈搜索。

## 验证和日志

```bash
cd /root/ros2_ws
source /opt/ros/humble/setup.bash
source install/setup.bash
colcon test --packages-select rmtc_bot
colcon test-result --verbose
```

开发联调脚本独立使用 ROS 公共域 40、内部域 41 和端口 8081，验证默认 100 ms 第一阶段及 50 ms 加速的完整 500 Tick 四车对抗，最后自动清理自己启动的进程：

```bash
python3 tools/integration.py
# 完整正式第二阶段节奏（约 4 分 10 秒）
python3 tools/integration.py --standard --stage2-only
# 不启动裁判，用合成公开感知验证坏消息、重复帧和换场恢复
python3 tools/wire_test.py
```

联调脚本可读取练习成绩作为测试断言，这些数据不会传给选手程序。测试结果在 `test-results/`，程序运行日志为 JSONL，包含动作、位置、回执、得分、任务、决策耗时和 AI 请求结果，不写令牌或 HTTP 请求头。真实 AI 成功资格以正式裁判审计记录为准，HTTP 2xx 和模型内容合法是两件事。

## CLion

C++ 包位于容器内 `/root/ros2_ws/src/rmtc_bot`；可将该目录作为 CMake 项目打开。构建脚本会导出 `/root/ros2_ws/build/rmtc_bot/compile_commands.json`，供 IDE 索引。构建和调试使用容器内的 ROS Humble 环境。
