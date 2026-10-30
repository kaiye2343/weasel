# PR 02：可重复长串基线工具与当前阻碍

## 当前结果

**工具草案已编译，原生测量受阻，没有新的 P50/P95 结果。** 6 项纯统计和候选语义契约通过；首次原生运行在创建会话前被严格资料目录检查阻止，详见[隔离问题单](issues/2026-10-30-long-input-baseline-isolation-guard.md)。遵循 AGENTS 交接规则，不重试或绕过该保护。

旧万象静态快照已缺失，另见[资料问题单](issues/2026-10-30-historical-performance-fixture-missing.md)。旧报告保留为历史，不能拿本轮小夹具与 2026-10-18 的完整万象耗时做前后对照。

## 复用与实际调用边界

CodeGraph 核对 `RimeWithWeaselHandler::ProcessKeyEvent` 的核心 scope 只包围 `rime_api->process_key`，其后 `_Respond` 和 `_UpdateUI` 在外；IPC 的 `_SendMessage` scope 包含服务端处理，不能与核心简单相加。已读旧 `CandidateProbe.cs` 的官方 C API 边界、`tests/rime_learning_test.cpp` 的独立资料／维护接口和现有探针说明。

新工具只复用这些已知接口与隔离方法，不改生产探针、不实现 C1c、不开启日常服务采样。C ABI 路径校验仍需高级模型确认。现有前端合成基准和原生阶段探针继续作为独立证据，不串成伪造端到端时间。

## 固定输入与拟执行方法

- 核心：x64 librime 1.13.1，强制固定 SHA256 `2D8F1BC3737635A11D9FB1BFCA4DC9E70533633930A8A0142A81CA879C39C45B`；不读取安装目录作为默认依赖。SDK／artifact 外部文件由执行者明确提供。
- 资料：只接受仓库 `tests/long-input-fixture` 的四个固定文件。合成 `baseline` 方案／词典 1.0，包含原生组句与用户学习能力，不含万象、Lua、语法模型或真实用户补丁。**这是独立夹具，不是关闭现有万象功能来跑分。**
- 语料：重复 `nihao`、`woxiangzaizhelishuruyijuchangyidianzhongwen`、`shijieshangderenming`，目标 8／16／32／64／128 个 ASCII 字母。均为版本化合成语料，不采集真实输入。
- 部署在一次性 builder，编译索引复制到专用 static。每案例／长度默认 1 轮预热、5 轮测量，每轮创建新的空学习目录和会话；不提交选词，结束清空 composition 并释放 session。预热也用独立空学习库，DLL、进程和 OS 缓存不重置，不冒称冷缓存测试。
- 保存原始逐键 API 时间、候选内容、注释和顺序、返回处理状态、页号与高亮；只含合成资料。提取、摘要、序列化和文件 IO 均不计入核心／取 context API 时间。
- 统计最近秩 `ceil(p*n)-1`：每个案例／目标长度合并该组重复的全部逐键样本，分别 P50／P95／min／max；同时保留每轮统计与最后一个按键的跨轮分布。不能把目标长度组的所有前缀按键 P50 当作“第 128 键”的延迟。仅 5 个最后键样本时 P95 等于最大值，样本量明显有限。
- 运行输出记录完整源码号、原始资料和编译资料哈希、实际核心版本、OS、运行时、CPU／位宽、计时器频率、预热和重复参数、顺序及全进程峰值 working set。峰值含部署／预热／报告分配，不表示每键保留内存。
- 同案例、长度跨测量轮比较候选内容／注释／顺序、处理返回、页号和高亮；差异保留报告但拒绝性能比较。静态资料及源夹具运行前后哈希须保持一致。无候选、未处理按键、超时或缺轮均不能算成功数据。

这些是实现契约，尚未得到原生运行验证；尤其部署、候选读取及路径 ABI 不能仅靠编译视为正确。每轮最多 8 分钟，外部 CI 还应提供进程超时。输出只允许仓库 `.tools/long-input-baseline` 的全新子目录，不清理旧结果。

## 阶段可测性

| 阶段 | 当前工具接口 | 当前实际结果 |
| --- | --- | --- |
| 核心计算 | 单次 `RimeProcessKey` 前后单调时间 | 尚未执行 |
| 读取 context | 单次 `RimeGetContext`，不含 Weasel 响应序列化 | 尚未执行 |
| 提取候选 | 托管字符串复制和 `RimeFreeContext`，单独计时 | 尚未执行 |
| Weasel 响应／IPC | 未创建 Weasel 服务、管道或响应解析链 | unavailable |
| 布局／资源／绘制 | 不创建 HWND、不初始化皮肤或渲染器 | unavailable |
| 呈现完成／应用端输入 | 没有真实按键到扫描输出测量 | unavailable |

不向 unavailable 阶段填 0，也不把核心与历史不同机器前端数字相加。托管调用和系统负载属于测量局限。

## 命令与验证记录

需要 .NET 10 SDK，以下命令在仓库根执行。首次运行需先由高级模型解除隔离问题：

```powershell
dotnet run --project tools/LongInputBaseline -c Release -- --self-test
# 原生路径当前受阻，不作为可用性能入口宣传：
./tools/run-long-input-baseline.ps1 -EngineDll <已核对摘要的x64核心文件> -Warmup 1 -Repeats 5
```

2026-10-30 本机 SDK 10.0.402：restore（无第三方包、NuGetAudit=false）及 Release 编译通过；`--self-test` 输出 `PASS 6 baseline statistics and candidate-order contracts`。原生调用一次即失败，原始信息见问题单，未取得任何有效组。没有安装、重启服务、修改日常资料或测试桌面。

PR 03：当前热点历史证据在外部 librime `MakeSentence → Dictionary::Lookup → Table::Query`，本仓库固定二进制不提供安全局部修改及收益证据。基线受阻期间不启动缓存／算法改动，不关闭组句、学习或模型制造收益。
