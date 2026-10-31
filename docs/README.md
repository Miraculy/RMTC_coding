# 文档索引

文档按主题归类，文件名保留原有名称和日期。当前构建、启动和接入方法见[仓库 README](../README.md)；历史报告中的参数、成绩和结论对应各自记录的版本与条件。

## 常用入口

- [第一阶段每 Tick 执行流程](stage1/STAGE1_TICK_FLOW.md)
- [第二阶段每 Tick 执行流程](stage2/STAGE2_TICK_FLOW.md)
- [第一阶段逐文件逐行解析](stage1/STAGE1_FILE_LINE_EXPLANATION_20261029.md) · [HTML 版](stage1/STAGE1_FILE_LINE_EXPLANATION_20261029.html)
- [第二阶段逐文件逐行解析](stage2/STAGE2_FILE_LINE_EXPLANATION_20261030.md) · [HTML 版](stage2/STAGE2_FILE_LINE_EXPLANATION_20261030.html)
- [开发环境记录](guides/DEVELOPMENT_NOTES.md) · [四车测试方法](guides/SELFPLAY.md)
- [完整历史验证记录](../VALIDATION.md) · [第二阶段发布说明](../stage2/README.md)

## 分类目录

| 目录 | 内容 | 文件数 |
| --- | --- | ---: |
| [guides/](guides/) | 本地环境、开发流程及四车测试方法。 | 2 |
| [rules/](rules/) | 规则迁移及版本变化对实现的影响。 | 2 |
| [stage1/](stage1/) | 每 Tick 流程、源码逐行解析、一阶段算法研究与验证。 | 14 |
| [stage2/](stage2/) | 每 Tick 流程、源码逐行解析、正式版本沿革与二阶段验证。 | 22 |
| [research/](research/) | 资源、战斗、得分、时序、文献及实验综合分析。 | 16 |
| [archive/](archive/) | 历史复查清单、任务提示、交接与已归档目标。 | 6 |
| [results/](results/) | 按批次保留的轻量结果、图表、补丁和校验信息。 | 13 |

## 环境与开发指南

本地环境、开发流程及四车测试方法。

- [本地开发上下文与原 README 备份](guides/DEVELOPMENT_NOTES.md)
- [四车独立策略测试](guides/SELFPLAY.md)

## 规则与协议对照

规则迁移及版本变化对实现的影响。

- [v1.1 规则迁移与策略依据](rules/RULES_V1_1.md)
- [RMTC v1.2规则对第二阶段当前策略的实际影响](rules/STAGE2_RULES_V1_2_IMPACT_20261026.md)

## 第一阶段

每 Tick 流程、源码逐行解析、一阶段算法研究与验证。

- [一阶段密集障碍算法实现与验证（2026-10-25）](stage1/STAGE1_DENSE_IMPLEMENTATION_20261025.md)
- [一阶段20×20、25%障碍报告复核](stage1/STAGE1_DENSE_REPORT_REVIEW_20261024.md)
- [阶段一 e075 验证摘要（2026-10-25）](stage1/STAGE1_E075_VALIDATION.md)
- [STAGE1_FILE_LINE_EXPLANATION_20261029（HTML）](stage1/STAGE1_FILE_LINE_EXPLANATION_20261029.html)
- [一阶段实现：逐文件逐行解析](stage1/STAGE1_FILE_LINE_EXPLANATION_20261029.md)
- [STAGE1_FILE_LINE_EXPLANATION_20261029_MANIFEST](stage1/STAGE1_FILE_LINE_EXPLANATION_20261029_MANIFEST.json)
- [一阶段交接：以原 subgoal-v4 直接优化（2026-10-25）](stage1/STAGE1_HANDOFF_20261025_SUBGOAL_V4.md)
- [一阶段联合规划实验与复杂图稳定性复盘（2026-10-25）](stage1/STAGE1_JOINT_RESEARCH_20261025.md)
- [100 Tick 一阶段：论文检索与所附算法方案评审](stage1/STAGE1_PAPER_ALGORITHM_REVIEW_20261025.md)
- [STAGE1_PAPER_ALGORITHM_REVIEW_EVIDENCE_20261025](stage1/STAGE1_PAPER_ALGORITHM_REVIEW_EVIDENCE_20261025.json)
- [一阶段入口风险与失败回退收益实现（2026-10-25）](stage1/STAGE1_SUBGOAL_IMPLEMENTATION_20261025.md)
- [原 subgoal-v4 资源访问顺序优化（2026-10-25）](stage1/STAGE1_SUBGOAL_ORDER_20261025.md)
- [第一阶段每 Tick 执行流程](stage1/STAGE1_TICK_FLOW.md)
- [一阶段观测路线与推演校准：实现及开发验证（2026-10-25）](stage1/STAGE1_VIEWROUTE_IMPLEMENTATION_20261025.md)

## 第二阶段

每 Tick 流程、源码逐行解析、正式版本沿革与二阶段验证。

- [远端更新与二阶段效果测试报告](stage2/REMOTE_UPDATE_CONTENTION_TEST_20261030.md)
- [二阶段参赛代码竞争力与优化方向分析](stage2/STAGE2_COMPETITIVENESS_REVIEW_20261027.md)
- [二阶段通用争格脱困 v1 实现说明](stage2/STAGE2_CONTENTION_RECOVERY_V1_20261029.md)
- [四车DeepSeek二阶段更新对照：已有结果与余额中断说明](stage2/STAGE2_DEEPSEEK_CONTENTION_TEST_20261030.md)
- [二阶段公平资源条件复评与优化试验](stage2/STAGE2_FAIR_RESOURCE_REVIEW_20261026.md)
- [本轮工作总结：二阶段公平资源条件下的复评与优化](stage2/STAGE2_FAIR_WORK_SUMMARY_20261026.md)
- [STAGE2_FILE_LINE_EXPLANATION_20261030（HTML）](stage2/STAGE2_FILE_LINE_EXPLANATION_20261030.html)
- [二阶段实现：逐文件逐行解析（2026-10-30）](stage2/STAGE2_FILE_LINE_EXPLANATION_20261030.md)
- [STAGE2_FILE_LINE_EXPLANATION_20261030_MANIFEST](stage2/STAGE2_FILE_LINE_EXPLANATION_20261030_MANIFEST.json)
- [二阶段最终交付：以排名为目标选择B1](stage2/STAGE2_FINAL_RELEASE_20261027.md)
- [二阶段优化思路：按机会损失规划资源与战斗](stage2/STAGE2_OPPORTUNITY_STRATEGY_IDEAS_20261028.md)
- [新鲜资源机会与探索保持：实现及筛查](stage2/STAGE2_RESOURCE_FOCUS_SCREEN_20261026.md)
- [第二阶段每 Tick 执行流程](stage2/STAGE2_TICK_FLOW.md)
- [v1.2堡垒位置补充：实现核验](stage2/STAGE2_V12_GEOMETRY_REVIEW_20261026.md)
- [本轮总结：公平资源优化与v1.2补充核验](stage2/STAGE2_V12_WORK_SUMMARY_20261026.md)
- [二阶段 v2.1（guard+F1）并入正式包记录](stage2/STAGE2_V2_1_GUARD_F1_ACCEPTANCE_20261029.md)
- [二阶段 v2.2：LLM 时效补丁](stage2/STAGE2_V2_2_LLM_TIMELINESS_20261029.md)
- [二阶段 v2.3：洁身自好事件期间的条件 DEFEND](stage2/STAGE2_V2_3_PURITY_DEFENSE_20261029.md)
- [二阶段 v2.4：天使恩泽的条件利用](stage2/STAGE2_V2_4_ANGEL_VALUE_20261029.md)
- [二阶段 v2（删 defend + 本象限 20R 优先）验收记录](stage2/STAGE2_V2_ACCEPTANCE_20261028.md)
- [二阶段 v2 代码分析与优化方向](stage2/STAGE2_V2_CODE_REVIEW_20261028.md)
- [第二阶段策略与验证方法](stage2/STRATEGY.md)

## 专项研究与复盘

资源、战斗、得分、时序、文献及实验综合分析。

- [连续观测下的追击与防御僵持](research/COMBAT_STALL_REVIEW.md)
- [死亡、复活与战斗后推进价值复盘](research/DEATH_RECOVERY_REVIEW.md)
- [低血量推进与下一Tick火线](research/DYNAMIC_FIRE_REVIEW.md)
- [历轮策略实验的综合复盘](research/EXPERIMENT_SYNTHESIS.md)
- [2026-10-24 异质四车策略验证](research/HETEROGENEOUS_BENCHMARK_20261024.md)
- [新方向：核查结论与实施记录](research/NEW_IDEA_IMPLEMENTATION.md)
- [资源分瓶颈反思：2026-10-24](research/RESOURCE_BOTTLENECK_REFLECTION_20261024.md)
- [新增文献资料与当前资源方案的对照复核](research/RESOURCE_LITERATURE_REVIEW_20261025.md)
- [开局探索与资源机会复盘](research/RESOURCE_OPPORTUNITY_REVIEW.md)
- [2026-10-24 资源争夺优化与自测](research/RESOURCE_OPTIMIZATION_20261024.md)
- [资源分阶段实验：2026-10-24](research/RESOURCE_PHASE_REVIEW_20261024.md)
- [资源优化分方案交付：方案一，失败回执驱动的备选采集](research/RESOURCE_RECOVERY_20261025.md)
- [得分提升复盘与异质四车测试安排](research/SCORE_RETROSPECTIVE_20261024.md)
- [Tick 利用率与事件预告复盘（2026-10-22）](research/TICK_REVIEW.md)
- [resource_timing_reflection_evidence_20261024](research/resource_timing_reflection_evidence_20261024.json)
- [score_retrospective_evidence_20261024](research/score_retrospective_evidence_20261024.json)

## 历史任务与交接

历史复查清单、任务提示、交接与已归档目标。

- [Codex 交接任务书：独立复查 RMTC 项目并产出与既有报告同等级的分析](archive/CODEX_HANDOFF_20261024.md)
- [RMTC v1.1 独立复查报告（2026-10-24）](archive/CODEX_INDEPENDENT_REVIEW_20261024.md)
- [Codex 补充复查：11 项之外的 6 个新方向](archive/CODEX_NEW_IDEAS_v1.1.md)
- [Codex 并行实验 Prompt：资源方向 A/B 测试](archive/CODEX_RESOURCE_DIRECTION_PROMPT_20261024.md)
- [Codex 复查清单（v1.1，裁剪版）](archive/CODEX_REVIEW_v1.1.md)
- [原阶段目标归档](archive/GOAL_ARCHIVE_2026-10-23.md)

## 结果与证据附件

按批次保留的轻量结果、图表、补丁和校验信息。

- [resource-recovery-20261025/README.md](results/resource-recovery-20261025/README.md)
- [resource-recovery-20261025/results.json](results/resource-recovery-20261025/results.json)
- [resource-recovery-20261025/source-sha256.json](results/resource-recovery-20261025/source-sha256.json)
- [stage2-contention-review-20261030/deepseek-summary.json](results/stage2-contention-review-20261030/deepseek-summary.json)
- [stage2-contention-review-20261030/default-off-entry-checks.json](results/stage2-contention-review-20261030/default-off-entry-checks.json)
- [stage2-contention-review-20261030/maps-deepseek.png](results/stage2-contention-review-20261030/maps-deepseek.png)
- [stage2-contention-review-20261030/maps-no-ai.png](results/stage2-contention-review-20261030/maps-no-ai.png)
- [stage2-contention-review-20261030/no-ai-summary.json](results/stage2-contention-review-20261030/no-ai-summary.json)
- [stage2-contention-v1-20261029/candidate.patch](results/stage2-contention-v1-20261029/candidate.patch)
- [stage2-contention-v1-20261029/implementation.json](results/stage2-contention-v1-20261029/implementation.json)
- [stage2-final-20261027/entry-smoke.json](results/stage2-final-20261027/entry-smoke.json)
- [stage2-final-20261027/selection.json](results/stage2-final-20261027/selection.json)
- [stage2-final-20261027/validation.json](results/stage2-final-20261027/validation.json)

结果附件保持各批次结构；原始结果 JSON、补丁和源码快照保留记录时的内容、路径及校验值。忽略目录中的本地原始比赛数据和仓库外归档不随这些轻量附件分发。
