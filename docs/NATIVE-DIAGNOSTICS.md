# 原生构建诊断门槛

## 输入与判定

`tools/check-build-diagnostics.ps1` 读取正式 Win32／x64 MSBuild 日志、两个架构的预览日志，以及唯一一组历史负控制日志。缺失、空日志、缺少架构或存在歧义控制组时失败，不能用不完整输入生成通过报告。

解析器 `tools/check-native-diagnostics.cjs` 只使用 Node 标准库；先去 runner 根、控制目录和明确的 SDK 根，再按项目、架构、来源、控制名、源文件、行列、编号及消息去重。支持 C、D、RC、MIDL、LNK、MSB 诊断，重复的编译／汇总只计一次。脚本命令和 `throw ... failed` 文本不是编译诊断；实际的未知编译选项警告仍会阻止交付。

CI 固定 `VSLANG=1033`。读取支持严格 UTF-8 和带 BOM 的 UTF-16LE；非法编码、残余 NUL、未知诊断语言或格式失败，不把无法识别的内容当作零警告。

所有 error、未批准 warning、已不再出现的基线条目都会阻断。`tools/native-warning-baseline.json` 初始为空：本轮修复警告，不建立覆盖目录或编号的宽泛豁免。今后若批准确切的第三方／历史控制诊断，必须写出九字段完整签名、原因及来源；生产和负控制、两个架构不可混用。未知外部路径按完整路径摘要区分并隐藏原始路径，禁止对未识别的外部依赖根建立白名单。

## 输出与检查

- `output/archives/build-diagnostics.json`：实际去重记录、通过／失败、未批准和未使用的基线。
- `output/archives/build-diagnostics.md`：生产警告、负控制警告、批准基线、错误等分类计数，追加到 CI 摘要。
- `output/archives/build-logs`：成功构建的原始日志；失败时由既有 failure artifact 收集根目录和控制目录日志及诊断报告。

本地回归命令：

```powershell
node --test tests/native_diagnostics_test.cjs
```

测试覆盖真实 CLI、双架构去重、精确白名单、非法／过期条目、定位归一化、BOM、坏编码、空／缺失输入和越界路径。对照旧 CI `39934338333` 的控制台日志可检出 17 条历史去重诊断；生产修正后的完整 CI `40006425435` 未检出 C／LNK／MSB 诊断，并通过 11 对 DIA 符号检查。控制台对照用于验证解析格式，正式门槛使用每个架构和控制组独立的原始日志，不混用来源。

Action 运行时和工具链弃用提示单独记录在 [Actions 依赖](ACTIONS-DEPENDENCIES.md)；它们不属于原生 C++ 编译计数，不隐藏这些提示，也不据“零原生警告”宣称全部依赖通知已经清零。
