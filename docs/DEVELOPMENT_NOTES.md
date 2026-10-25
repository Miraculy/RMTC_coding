# 本地开发上下文与原 README 备份

- 最新状态（2026-10-24资源分阶段实验，以本条为准）：原正式源码/入口未改、安装仍994515d86776a63950b272ab388471f29e9726ec148d3770032028e0688b4267。独立副本experiments/resource-phase-20261024基于42bb7ce，候选b947bb99...，140项检查通过（124核心+14HTTP+2CTest）。新增opening_high_value=false（前80Tick）与resource_cap_switch=false，原opening_route_information也false。四图全席位16对：开局平均R50→44.375、分38.9625→35.81875，3增7降3掉名次，资源0增6降，高值任务10/16及三任务7/16均不增、总任务38→37，否决；满额切换分数/名次/资源全部不变，只有1例达到120，任务298→55但不能当新增完成率。补内置陪练4对3例满额，分数/第一不变，含任务198→396、死亡12→15反例，不能宣称更安全。等代价视野4案例8次实际移动，16对无最终收益，继续关闭。主/陪练不混率。7批74完整场136000决策、66四车接口，零非法/拒绝/缺回执及Tick/类型不符，maxP95 1.436744ms/max24.394155ms；默认2场4000逐字段决策/侧与9945等价。所有句柄结束、残留比赛进程0；无新真实模型/500ms/组合验证。证据docs/RESOURCE_PHASE_REVIEW_20261024.md及副本reports/，补丁resource-phase.patch；不要重跑已完成批次。旧目标继续paused并存档；下一步应保留确定普通资源链，研究满额但D未封顶时远任务的机会成本/退出，别硬探索或无条件任务优先。

记录日期：2026-10-21。本文件用于保留维护者环境和后续开发所需的上下文，不作为所有协作成员的环境要求。

## 后续维护时需要记住

- 最新状态（2026-10-24，以本条为准）：继续新方向第二项，已实现purity_joint_actions=false，仅单个可见敌人且视野覆盖其潜在贡献者范围时，对本车攻击分开估计敌方攻击/防御，分支内算贡献、击杀、死亡扣60、D封顶，反伤不乘pan；沿用防御0.65/0.15启发先验，其余保守按开火，非已知敌方动作。多敌/视野不足/保护/启用中的forecast等保持旧处理，防御反杀奖励尚未实现。安装6332c49eb5e70ed58fd3900f26d9b0d74d08b55c61d5d06c8c1ebdd6cf4d7ed3，130检查通过（114核心+14HTTP+2CTest）。四图四对500Tick真实单事件：候选38个Tick触发但所有四车行动全同，分数37.15/40/26.2/32.5及名次3/2/4/4均不变；0增分0退步且双方0第一，不能当稳定获胜或修正无用。焦点56/56真实HTTP及结构接受，首次提前33–37Tick、事件参数正确；实际未遇到旧误判必死而新可存活的关键局面，证据目前来自规则单测。保留关闭，不补跑重复场。四场无事件分别复现上一版默认和开关未触发，各4000逐字段决策等价。新6批12场24000决策12接口完整、零非法/拒绝/回执缺失及Tick/类型不符，maxP95 4.281691ms/max16.942638ms。385文件未含密钥，私密600；所有构建、比赛、审计已结束。产物在purity-joint-candidate-known-1792795787459385267/round-audit.json等，详见VALIDATION.md与NEW_IDEA_IMPLEMENTATION.md。原目标继续paused并存档，不恢复、不标完成；event_revisions/fortress_time_budget仍false。下一项按计划研究开局高价值资源的发现窗口与可赢路线，尚未编码；已有5/6缺高值案例开局未见，不能直接提高高值权重。

- 2026-10-23新方向最新状态：用户要求保存原目标并尝试新分析；旧目标paused、未完成，见GOAL_ARCHIVE_2026-10-23.md。新方向逐项判定和阶段结果NEW_IDEA_IMPLEMENTATION.md。安装0b935beffd52c9c86fa4c27a8221a54200638e84f5e44d44abbc1bfe03c9914c，125项检查；event_revisions=false。首版对同几何参数更新过严，真实场出现4次缺引用拒绝；修订保留旧精确身份更新并递增版本，几何/激活Tick修正仍靠LLM引用已知ID，不按同类同Tick强合并。两版各4/4真实合成样例通过，后者冻结benchmark；完整新配对均32.5/第四，各56/56HTTP成功结构接受+1HTTP502，八类首次解释全迟到，不能当正式提前条件下收益。共7批10整场20000决策、10接口，无非法/拒绝/缺回执/Tick不符；8原始类型差异已核为世界分裂强制WAIT，无未解释差异。默认三批6场12000决策/配置/比分与5edc...等价。旧堡垒9批14场收尾、4对隔离无增分且一例掉名次，继续关闭。773文件无密钥、私密600，所有句柄/进程结束。下一项purity互斥与开局发现尚待实现，不能把新方向说成全部完成。16场基线5/6缺高价值案例开局未见，另1已优先追但对手明显更近；资源总量200也不允许四车都到120，切勿仅凭距60满分缺口普遍加权。完整记录VALIDATION.md末尾。

- 最新状态以本条为准：安装69307a06...，118项检查；safe_progress_moves/respawn_task_route/survival_conditioned_progress均false，原默认攻防与追击保留。四图16组四臂实验，安全首步单项无最终收益，任务路线与组合均掉分，组合C36.15→30.75，局部避火未保证任务完成；DYNAMIC_FIRE_REVIEW.md及VALIDATION.md末尾保留全部反例。两场无覆盖默认与8d619...4000决策/比分/配置等价。当前哈希双真实默认事件消融32.5→57.5、第三→第一，本车各22/22成功并接受、首次提前7/8Tick，+25为资源20+任务5；全场包含陪练35/36次调用，不能算成本席位。已知定向幻术压力场不证明正常场普遍收益。本轮11批68场133000受控决策66接口完整零异常；累计154归档中152完整、一批2/8、一批零场，971整场1484500决策666接口，131脚本快照匹配/23早期缺。19243文件无密钥、私密600、全部句柄终结/进程0。复活按用户初始点曼哈顿及上右下左等补充，使用权威自身坐标；下步独立检查提前事件到位/触发/驻留机会成本，现有默认已参考未来事件，不盲开旧event_timing组合，也不继续只围绕单一任务反例调参。仍未跨图稳定第一，勿重启旧实验。

- 最新状态以本条为准：8d619a8c...安装构建，114项检查；survival_conditioned_progress与respawn_task_route均false，没有新默认收益。三版27批198整场324000受控决策150接口全部完整/零非法/拒绝/回执异常，最大P95 1.483397ms/max8.723999ms；累计903整场1351500决策600接口，143归档中141完整、早期一批2/8、一批零场。17890文件无测试密钥、私密600、所有句柄/进程结束。存活分支使取消移动5→0但得分/第一无变化；复活后路线虽多做任务却36.15→33.95，期限保护仍36.15→33.15，未推广。三版默认各两场四车回归、12000决策/比分等价，新增日志仅为本车alive/protected/public_missions/initial_spawn；本轮无模型调用，原模型增益仍归d332...。合法历史复核普通复活4Tick及一例D原点被C占时落上格，不能泛化并发规则验证。新复盘DEATH_RECOVERY_REVIEW.md、证据VALIDATION.md末尾；下步先查低HP路线中敌车一步变向造成火线是否有较安全合法走法，勿盲开旧预测参数，勿重启结束的实验，仍未跨图稳定第一。

- 当前状态以本条为准（以下旧条为历史）：安装dc197c2f...，107项汇总检查；新竞争探索与延后前期追击均默认关闭。首版竞争探索三类对手没有整体收益；延后追击虽首批第一4→7/16，追加四新图却4→3/16且平均分下降2.73125，完整当前组8→10/32第一但均分38.61875→37.3484375、含丢冠，未推广。已有攻防/追击默认保持，两构建各两场无覆盖四车500Tick与d332...全部关键决策和比分等价。新24批180场312000受控决策148份接口完整且零非法/拒绝/回执异常，最大P95 1.65163ms/max9.480689ms；累计705有效完整场1027500决策450份接口，116份归档中114完整批次、另早期一批仅2/8场完成、一批启动失败零场。用户补充初始出生点曼哈顿分层、上右下左搜索、障碍不阻断选点、资源允许及A→D占位次序，见RULES_V1_1.md；当前不预测精确落点，始终用感知坐标，新增备用落点不改spawn/漏初始帧/换场回归通过。后续死亡与任务恢复的机会成本应按合法信息研究。本轮无新模型调用，原增益归属d332...实测。详见VALIDATION.md末尾与RESOURCE_OPPORTUNITY_REVIEW.md，旧实验不要重启，尚未稳定第一。

- 当前安装d332e3c0...，102项汇总检查，已默认启用standoff_robust_trade及此前追击修正。防御倾向对手八图32对第一9→13、均分+5.371875、无掉分/掉名次；另当前对手8对与内置16对均不变。固定概率攻击版有42.5→39.8反例仍关闭。默认两场500Tick逐帧复现42.5/47.5分第一，四车全新默认压力两场完整。当前真实模型两个长提前场景无额外分数，旧版收益不沿用；更早幻术d332直接35→57.5第二→第一，双方首次解释提前6Tick、22/22real及结构接受。四车标准事件联调虽增分但解释部分迟到，单列不当作正式前提证明。本轮156场255000决策，全新版累计92批525场715500决策、302份四车接口快照通过，10506文件无测试密钥、私密600，所有句柄/进程已结束。下一步前期探索机会的合法日志诊断见RESOURCE_OPPORTUNITY_REVIEW.md，尚未改资源策略；全证据和失败见VALIDATION.md与COMBAT_STALL_REVIEW.md。仍未稳定第一。

- 上一轮历史安装8542b520...，97项汇总检查，break_pursuit_cycles=true，其他新风险/坐标优先/资源竞争实验保持false。四车独立六图24对第一6→7且无掉分、另内置对手16对全部不变；默认完整500Tick已复现唯一+13.5分案例。四车接口和单车正式入口边界见SELFPLAY.md，逐轮反例和冻结版本见COMBAT_STALL_REVIEW.md与VALIDATION.md。坐标优先首版掉分、满分保护版16对没有分数收益，未推广；防御风险均分虽升但有30→15.4退步，未推广。实际默认配置的两组按时真实事件解释分别+7.5、+5资源分，另一组反馈到生效同Tick单独保留，不冒充提前反馈证据。最新累计新版63批369完整场/460500受控决策，184场四车接口核对，7376文件无测试密钥、私密600。所有句柄与进程均结束，不重启旧批次；尚未跨图稳定第一。

- 上一轮历史安装1c737df2...通过90项汇总检查。资源竞争实验内置陪练有一胜但四车对抗掉分，contested_task_resources保持false；score_aware_combat=true、damage_reserve=120不变。用户授权开发时四车同时运行，正式仍一车，入口与数据隔离见SELFPLAY.md。四车1对3/2对2共16场32000决策和全基线2场已结束，接口审计及反例见VALIDATION.md。双真实解释消融在两个50Tick幻术压力图增分，但均显式开启资源竞争实验，不能声称实际默认的同场收益。下一步防御僵持/追击往返分析仅用各车自己的日志，不将其他车私有记忆合并到策略。

- 2026-10-23按用户要求替换为v1.1裁判，活跃路径仍为容器`/root/rmtc_practice`；旧版完整备份`/root/rmtc_practice.previous-1792711906172071124`。新core指纹b3912533...、包哈希和迁移约束见RULES_V1_1.md。旧版结果不与新版混合。首场新裁判500Tick已通过。已完成六图24对与真实代理对照，默认score_aware_combat=true、damage_reserve=120；当前安装1300b2...通过88项汇总检查和两场默认500Tick逐帧等价核对。新版14/24第一数不变，仅一场名次改善，不宣称稳定第一。


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

2026-10-24异质四策略工具已落地；38场含新图16对+集成2+定位4，76000决策完整且合法审计通过。早期→当前均分+1.89375但第一4→3，资源/三任务仍短板；两个反例定位到robust触发，未回退正式配置。详见HETEROGENEOUS_BENCHMARK_20261024.md和实验reports。旧目标继续暂停，后续先做机会成本保护及未见图/邻位验证，勿重跑已完成批次。


2026-10-25：已实现第一阶段预算资源路线、合法已知区域基线预测、未知价值上界及路线保留，全部位于experiments/stage1-dense-20261024；正式入口未修改。230场配对测试显示稳定小幅增分，但原15图满分仍0且均分增幅未到+2，不推广。详细方法、失败变体和逐图结果见STAGE1_DENSE_IMPLEMENTATION_20261025.md；可通过独立run_stage1_candidate.sh显式启动冻结候选。
