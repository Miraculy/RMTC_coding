# 项目维护上下文

- 参赛程序使用 C++17 和 ROS 2 Humble。Python 脚本仅用于开发测试。
- 协作成员既有原生 Ubuntu 环境，也有 Docker 环境。README 以通用仓库根目录和原生 Ubuntu 流程为主，Docker 为可选方式；不要把维护者的容器路径当成所有人的路径。
- 维护者现有容器、路径、网络、旧 README 全文和后续工作上下文保存在 `docs/DEVELOPMENT_NOTES.md`。涉及本地环境或联调时先查阅它。
- 实测结果与验证局限保存在 `VALIDATION.md`。已完成的 AI 联调使用 mock，尚未验证真实 LLM 理解或正式 AI 调用资格。
- `tools/integration.py`、`tools/wire_test.py` 当前仍有维护者环境的固定路径，不能未经调整作为跨环境通用入口。
