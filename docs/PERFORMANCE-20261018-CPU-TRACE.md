# 长串输入 CPU 调用栈采样（2026-10-18）

## 结论和状态

用户明确授权管理员 CPU 采样并本人确认 UAC。本轮采样已完成，WPR 确认不再录制。**本轮定位热点，尚未修复输入延迟。** 日常服务、安装核心、输入配置和学习数据库均未更改。

主要可靠热点位于 `ScriptTranslation::MakeSentence → Dictionary::Lookup → Table::Query`。这条链在未创建候选窗、未加载皮肤图片的独立算法进程中出现。不能以此排除用户真实桌面上另有绘制延迟，但皮肤优化无法消除本轮观察到的这些计算峰值。

## 采样与符号核验

- WPR 内置 CPU 配置，独立实例 `RimeIsolatedCpu-4cc0cb697a4144f7991851b5d0c6a781`。
- 隔离测试进程 PID 37416，合成输入和静态资料与此前隔离对照相同，未使用真实学习库。
- 测试约 10 秒结束，11:37:50 完成 ETL 合并；采样文件仅保存在本机私有 `.tools`，未上传系统级 ETL。
- 该进程 10131 个 CPU 样本，10125 个有调用栈，EventsLost=0。范围包含进程启动、准备和测试，比例不是纯输入阶段的独占墙钟时间。
- 分析使用已有 Microsoft TraceEvent 3.2.4 包。分析器编译成功；NuGet 漏洞服务当时不可达，产生 NU1900，不能称为完成漏洞检查。
- 从[官方 librime 1.13.1 发布页](https://github.com/rime/librime/releases/tag/1.13.1)取得对应 x64 SDK 和 PDB；SDK DLL 与实际安装 DLL 的 SHA256 完全相同：`2D8F1BC3737635A11D9FB1BFCA4DC9E70533633930A8A0142A81CA879C39C45B`。
- 符号 GUID `c3f57554-f90e-4390-8896-c2d2350aa7bf`、Age 1 与跟踪内 DLL 一致，符号读取器确认匹配。
- 某些优化地址被符号读取器标为 `??`、NOT IN RANGE；尤其表面排名靠前的 `??luaC_step` 不能作为精确函数归因，本报告不据此认定 Lua GC 为根因。

## 可靠的包含调用样本

这些比例包含被调函数，彼此嵌套，**不能相加**。完整摘录见 [CSV](benchmarks/2026-10-18-cpu-sampled-hotspots.csv)。

| 函数 | 样本数 | 占进程 CPU 样本 |
|---|---:|---:|
| ConcreteEngine::TranslateSegments | 6538 | 64.53% |
| ScriptTranslator::Query | 5072 | 50.06% |
| ScriptTranslation::MakeSentence | 4784 | 47.22% |
| Dictionary::Lookup | 3927 | 38.76% |
| Table::Query | 3680 | 36.32% |
| TableQuery::Access | 1353 | 13.36% |
| Poet::MakeSentence | 554 | 5.47% |

最多的一条完整调用栈有 905 个样本，其可靠核心部分为：

```text
RimeProcessKey
  ConcreteEngine::ProcessKey
    Speller::ProcessKeyEvent → Context::PushInput
      ConcreteEngine::Compose → TranslateSegments
        ScriptTranslator::Query → ScriptTranslation::Evaluate
          ScriptTranslation::MakeSentence
            Dictionary::Lookup → lookup_table → Table::Query
              TableQuery::Access
```

## 对应源码和解释

[1.13.1 ScriptTranslation::MakeSentence](https://github.com/rime/librime/blob/1.13.1/src/rime/gear/script_translator.cc#L568) 遍历当前音节图各起点，查询用户词典和静态词典，再把词图交给造句器。

同文件的 `EnrollEntries` 在词典查询完成后才按 `max_homophones` 截取同音候选。因此此前把 8 调到 4 未明显缓解前面的查表开销，与此调用链吻合；这仍不是所有输入情形的普遍结论。

[Table::Query](https://github.com/rime/librime/blob/1.13.1/src/rime/dict/table.cc#L552) 沿音节图遍历词典索引。长串涉及更多起点和分词路径，符合用户“越长越明显”的反馈。采样说明应优先检查此查询／构图阶段，不能仅按总耗时推断已有可安全复用的结果。

下一步候选方向是减少重复静态查询，或在核心中做受限缓存。实施前必须证明缓存键覆盖音节图变化、词典生命周期和配置切换，设置内存上限，并用候选结果回归验证；不缓存或覆盖活动用户数据库，不为了跑分关闭整句能力。

## 官方新核心兼容性探查

查询到[官方稳定版 1.17.0](https://github.com/rime/librime/releases/tag/1.17.0)。仅在隔离目录试验，没有更换日常 DLL。

- 直接加载旧索引出现 `Error loading prism`；空候选下的极短计时无效，不能称为加速。
- 仅重建主方案后主词典可加载，但英文、混合码、短语索引仍报错。该组亦不作为完整兼容或性能收益证据。
- 因此任何后续核心升级须先在隔离资料中完整重建所有依赖并验证中文、英文、混输、短语和模型，再进行同条件性能对比。发布版本较新或构建成功都不代表本问题已解决。

## 保留与交付

私有记录：`.tools/latency-diagnostics-20261018-110914/`，含 `cpu-target.json`、`cpu-trace-result.json`、ETL／ETLX、PDB 解析日志及分析 JSON。跟踪约 655 MB，包含系统级 CPU 信息，仅本机保留；公开提交只包含上述函数／计数与方法说明。

最终核对日常服务 PID 仍为 14812，启动时间未变；日常 default.custom、工作 custom、日常 custom 哈希均不变，实际 `rime.dll` 哈希不变。现有问题仍待核心级优化和用户实机验收。
