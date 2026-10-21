# 项目维护上下文

- 参赛程序使用 C++17 和 ROS 2 Humble。Python 脚本仅用于开发测试。
- 协作成员既有原生 Ubuntu 环境，也有 Docker 环境。README 以通用仓库根目录和原生 Ubuntu 流程为主，Docker 为可选方式；不要把维护者的容器路径当成所有人的路径。
- 维护者现有容器、路径、网络、旧 README 全文和后续工作上下文保存在 `docs/DEVELOPMENT_NOTES.md`。涉及本地环境或联调时先查阅它。
- 实测结果与验证局限保存在 `VALIDATION.md`。2026-10-21 已通过 DeepSeek 官方 `deepseek-flash` 关闭思考的 12 个有限事件样例及合成 ROS 闭环，完整四车对抗仍使用 mock，正式 AI 审计资格未验证。
- `run_deepseek.sh` 用于本地直连官方 API，密钥通过 `DEEPSEEK_API_KEY` 或隐藏输入提供，不写仓库；正式比赛保留 `gateway` 模式和本场令牌。真实测速工具必须显式选择执行，会产生 API 费用，不能放进默认单元测试。
- `tools/integration.py`、`tools/wire_test.py` 当前仍有维护者环境的固定路径，不能未经调整作为跨环境通用入口。
