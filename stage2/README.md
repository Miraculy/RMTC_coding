# 二阶段排名导向发布版

本目录固定完整17af源码和B1参数。C++源码逐文件等同于仓库提交 `f049728` 中的 `src/rmtc_bot`；运行时只将 `resource_blocked_recovery` 从原默认false改为true。其余规划、事件和修订开关在 `config/final.yaml` 中明确固定。源码及参数校验值见 `SOURCE_MANIFEST.json`。

从仓库根构建并运行：

```bash
./build_stage2.sh
# 按主办方要求设置ROS域、AI_GATEWAY和本场RMTC_TEAM_TOKEN后：
./run_contestant.sh A 2
```

也可直接运行 `./run_stage2_final.sh A`。脚本使用仓库相对位置寻找自己的安装产物，不依赖维护者绝对路径；后置ROS参数可显式覆盖配置。源码复制自已验证版本，未合入探索候选或一阶段算法。

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
