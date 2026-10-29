# 二阶段排名导向发布版

当前工作树为基于 `4d632d3`（v2.4）的**通用争格脱困源码候选 v1**，在 `config/final.yaml` 启用 `movement_blocked_recovery`。同一移动边连续两次收到匹配的本车 `E_BLOCKED` 后，短暂冷却该边并重算路径；保留事件解释时效、guard+F1、纯净防御和天使利用改动。详见[实现说明](../docs/STAGE2_CONTENTION_RECOVERY_V1_20261029.md)。按本轮用户要求，未编译、未运行测试或比赛，安装目录尚未更新，不能据此宣称脱困或增分已经验证。

首版来源为17af源码和B1参数，曾逐文件等同于提交 `f049728` 中的 `src/rmtc_bot`；后续版本沿革见下文，当前源码已不再逐字节等同该祖先提交。当前源码及配置校验值在 `SOURCE_MANIFEST.json`，其中历史二进制哈希不代表本轮候选构建。

从仓库根构建并运行：

```bash
./build_stage2.sh
# 按主办方要求设置ROS域、AI_GATEWAY和本场RMTC_TEAM_TOKEN后：
./run_contestant.sh A 2
```

也可在构建后运行 `./run_stage2_final.sh A`。脚本使用仓库相对位置寻找自己的安装产物，不依赖维护者绝对路径；后置ROS参数可显式覆盖配置。当前源码候选须先构建才会被入口使用；关闭新功能可传入 `-p movement_blocked_recovery:=false`。

需要 Ubuntu 22.04、ROS 2 Humble、colcon、nlohmann-json3-dev、libcurl4-openssl-dev，完整安装说明见根README。非系统位置的依赖可通过CMake参数指定，例如 `./build_stage2.sh -Dnlohmann_json_DIR=/依赖前缀/lib/cmake/nlohmann_json`；仓库不写死本机依赖目录。

运行已有核心及本地HTTP测试（不启动裁判、不调用付费模型）：

```bash
source /opt/ros/humble/setup.bash
colcon --log-base stage2/log test --base-paths stage2/src --build-base stage2/build --packages-select rmtc_bot
colcon test-result --test-result-base stage2/build --verbose
python3 tools/verify_stage2_release.py
```

`COLCON_IGNORE`仅阻止根工作空间扫描本目录；`build_stage2.sh`显式选择`stage2/src`。一阶段源码和根安装目录保持独立。直接使用根包的 `ros2 run` 不等于启动本发布版，应使用上述入口。

本版是在用户最终排名目标下对已测方案的发布选择，尚未证明正式赛稳定前二或所有场景优于原版。已知堡垒驻留奖励窗口边界保留为后续问题，本次不夹带未经完整验证的修正。详见[选择依据与验证](../docs/STAGE2_FINAL_RELEASE_20261027.md)。


## v2 发布（2026-10-28，用户接受）
- 变更：阶段二不再生成 defend；开局 Tick<40 本队象限内 20R 已观测资源目标价值 +6。
- 依据：B1 12图×4=48局、B2 8图×4=32局本地 A/B（AI 关闭、冻结对手）：平均名次 2.31→2.17 / 2.56→2.19，前二 28→30/48、14→20/32，严格第一 14→17/48、10→13/32，死亡 229→191、157→135，D 损失 10080→7755、9290→7770，defend Tick 4303/4208→0；R 约 -0.6（已知边界）。
- 校验：build_stage2.sh、colcon test（136+14 通过）、tools/verify_stage2_release.py。
- 详见 docs/STAGE2_V2_ACCEPTANCE_20261028.md。


## v2.1 发布（2026-10-29）
在 v2（无 defend + 开局本象限 20R +6）基础上，启用 guard+F1：
- observation_detour=true（extra=2, attempts=1, min_tick=8, max_known=1）；
- resource_arrival_value=true（到达可获得性替换竞争折扣）。

修正评测（AI 关闭、mock、冻结对手）配对结果：B1 R 50.88→51.76、名次 2.059→1.853、前二 23→26；B2 R 持平 49.38、名次 2.125→2.062、前二 20→21。B1 死亡 8.24→9.06；R ≥ base+2 未达成，真实 LLM 条件未验证。详见 docs/STAGE2_V2_1_GUARD_F1_ACCEPTANCE_20261029.md。


## v2.2 发布（2026-10-29）
LLM 时效补丁：事件话题与 perception 只提交“预告”公告，派生行不再排队。S03/S04 真实 DeepSeek 测试中请求 50→17–18，事件解释从晚 14–142 Tick 变为提前 1–4 Tick；决策改变 383/500、394/500，但得分/名次/R 未提升。详见 docs/STAGE2_V2_2_LLM_TIMELINESS_20261029.md。


## v2.3 发布（2026-10-29）
洁身自好事件期间条件启用 DEFEND：默认关闭，final.yaml 打开。封闭 3×3 测试中，开启后 A 使用 12 次 DEFEND、HP 稳定在 60；关闭时 Tick 8 阵亡。正常 S03/S04 未触发，也没有回归。详见 docs/STAGE2_V2_3_PURITY_DEFENSE_20261029.md。


## v2.4 发布（2026-10-29）
启用天使恩泽条件利用：资源清空后才考虑，且要求缺失 HP 能抵消路程（missing >= 2*travel+4）。S03 短距离回血 60→100 并第一；S04/T02 长距离不绕路、保持第一。详见 docs/STAGE2_V2_4_ANGEL_VALUE_20261029.md。
