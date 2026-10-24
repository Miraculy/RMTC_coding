# 四车独立策略测试

正式比赛仍只启动本队一个席位。此处仅为开发时同时运行四个独立选手进程，用于更换对手策略、轮换席位和检验资源竞争；四车各自争取最高排名，不进行协同控制。

## 信息边界

- 每辆车只订阅本席位perception，以及公开mission、event_text、team_registry。程序自己的World、模型队列、模型上下文和日志位于独立进程，不合并观察或互传决策。
- 每辆车只发布本席位action与registration，节点名不同。ROS自带的parameter_events/rosout仅为框架接口，不作为比赛信息输入。
- 开赛前测试驱动核查四个节点实际的订阅与发布接口，将结果保存为每场`*-interfaces.json`。缺少必需接口、发现额外比赛接口或发现超时，直接停止该批次，不放宽为读取其他车感知。
- 测试驱动配置地图、启动和停止进程、赛后读取成绩；不向选手提供配置文件、全图、观赛状态或其他车的日志。场景种子也不传给选手。
- 真实AI测试只使用`gateway`或`gateway-real`。每车携带自己的席位令牌；真实上游测试密钥只在练习裁判代理内使用，不传给四个选手。公开公告允许四车分别获得，但模型记忆不在车辆之间共享。

## 轮换方法

在已构建并加载ROS环境的工作空间执行：

```bash
python3 tools/strategy_eval.py \
  --judge-root /path/to/rmtc_practice \
  --label selfplay-one --variants interior59341,map62543 \
  --seats A,B,C,D --no-events --opponents self --candidate-count 1 \
  --param standoff_robust_trade:=true \
  --opponent-param standoff_robust_trade:=false \
  --opponent-param break_pursuit_cycles:=false \
  --domain 82 --port 8084
```

`--opponents self`关闭所有内置陪练，启动四个C++选手进程。`--param`用于本场候选策略，`--opponent-param`用于其他车辆的基线策略，两者都只能修改白名单内的本地规划参数。

`--candidate-count 1`时，依次只有A、B、C、D使用候选，其余三车使用基线，更接近正式的一车对三车。`--candidate-count 2`时，依次为AB、BC、CD、DA使用候选；这是同局两种策略的分配，不是两车协作。两个模式必须分开统计。三辆或四辆候选可用于压力测试，四辆相同策略之间产生第一名不证明策略优于基线。

`--seats`是候选策略轮换的起始席位。每个地图、起始席位、重复次数各是一场完整比赛；同一场四个车辆记录不能当作四场独立比赛。

## 结果与限制

每场`summary.json`的`controllers`记录四车各自的策略参数、分项、排名、日志位置、Tick覆盖、非法动作、感知拒绝与决策耗时。任何一车不完整覆盖0–499或存在非法动作/感知拒绝，整场不计有效。冻结的选手二进制与测试脚本均保存并校验SHA-256。

至少同时看候选与基线的分数、资源、严格第一数、地图和席位差异，不能只挑本局赢家。仍要保留与内置陪练的测试，以检查结论是否只对同类策略成立。固定地图的重复场仅为复现证据，不增加独立地图样本。

`--no-events`、AI-off场次用于本地策略测试，练习原始任务分不证明正式AI资格。事件解释收益对照须让两组都通过真实测试网关调用模型，仅切换`use_event_interpretation`，并核对解释返回早于事件生效。练习网关成功也不替代正式网关审计。

实测结果及失败批次见工作空间根目录的VALIDATION.md。


完整批次可使用只读比较工具复核，控制组和候选组必须为同一冻结二进制、同一对手配置、同一地图/席位/重复次数及实际部署配置：

```bash
python3 tools/compare_eval.py \
  --control test-results/control-batch-one test-results/control-batch-two \
  --candidate test-results/candidate-batch-one test-results/candidate-batch-two
```

工具逐车核查完整Tick、非法动作、感知拒绝、比分公式及排名，四车模式还核查实际接口快照；输出掉分、掉名次和分项变化。存在回执的记录另核查其Tick，缺少回执不等于已经证实执行。不同版本默认入口的逐帧等价复验另行记录，不混入同一冻结程序的消融结果。


当前示例保留三辆对手的旧追击行为，用于复现防御倾向的对手配置；另须去掉break_pursuit_cycles覆盖检查当前默认对手，并保留内置陪练。每个配置均需自己的关闭/开启配对，不能合并不同对手的第一率。若重测固定概率实验，须显式关闭已推广的standoff_robust_trade，避免两个实验混在一起。动作回执现在分别报告Tick、动作类型不匹配和缺失数量；类型匹配只证明回执对应某个已提交动作，不证明每个同Tick修订都执行。

真实模型对照还须检查随机反馈造成的混杂：相同程序、地图、参数不保证模型内容或返回Tick相同。`tools/event_pair_audit.py`使用相同`--control/--candidate`参数，分别定位四车自己的首次动作、目标及已记录观察差异；若对手早于焦点车改变行动，最终分差不能单独归因于焦点参数。结构接受不等于语义正确，未发现早期差异也不构成完整的因果证明。

可单列事件解释关闭的对手组：双方同用`--opponent-param use_event_interpretation:=false`，焦点车双方仍使用真实模型解释，仅切换待测参数。其他车仍独立调用模型并仅接收自己的合法输入；不伪造调用或注入裁判配置。这种对手类型的结果不可混作默认事件对手组表现。
