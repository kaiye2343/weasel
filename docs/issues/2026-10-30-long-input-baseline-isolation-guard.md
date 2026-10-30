# 新隔离性能工具在会话创建前被资料目录校验阻断

状态：交接高级模型，停止该路径运行、插桩或修复尝试。PR 02 没有有效原生性能数据，PR 03 仍阻塞。

## 首次运行证据（2026-10-30）

在非管理员、受限开发 shell 中使用现有 `.tools/dotnet` SDK 10.0.402 和 `.165` artifact 内 x64 `rime.dll`，摘要为固定 `2D8F1BC3737635A11D9FB1BFCA4DC9E70533633930A8A0142A81CA879C39C45B`。该文件来自公开 librime 1.13.1，与既有对照所用核心一致。

工具为本轮尚未提交草案，运行时 HEAD `ce0b6e6`；以下新文件的实际代码随 PR 02 提交保留。仅运行一次，没有切换 ASCII 路径、放松断言、重试提权或调用旧脚本绕过。

```text
dotnet run --project tools/LongInputBaseline -c Release --no-restore --
  --engine .tools/release-5dff81f/tools/rime.dll
  --output .tools/long-input-baseline/local-12bc76c3cf7b4bc68ba9529cfbf11d94
  --revision ce0b6e60b0614761d73945fa4b73699fd9193fb7 --warmup 1 --repeats 5

Unhandled exception. System.InvalidOperationException:
  Rime user root isolation failed before session creation
  at NativeRime.Open(...) NativeRime.cs:line 49
  at Baseline.Run(...) Program.cs:line 62
```

运行目录保留 `.isolated-baseline`、生成的 builder/static 合成输入资料。没有 `report.json` 或有效 trial 样本，不把源代码中实现的预热／重复／统计写成已执行结果。

以上命令为首次运行记录，不是批准再次执行；接手获准复现时必须选择全新的输出子目录，现有目录受到防覆盖检查保护。

## 入口、影响与未确认项

- `NativeRime.Open(builder, builder, true)` 执行 `RimeSetup` / `RimeInitialize`，随后将官方 `RimeGetUserDataDir()` 返回值转换为字符串，与预期 builder 完整路径比较。
- 严格比较失败，scope 清理后抛出；外层尚未调用 `RimeStartMaintenance`、`RimeCreateSession` 或 `RimeProcessKey`。没有连接日常服务或运行桌面输入；脚本没有主动读取／复制日常资料或打开活动数据库。不过 `RimeInitialize` 已执行、原生实际返回目录未知，不能宣称已证明原生初始化全程隔离；该初始化内部是否另有访问需接手者核对，当前没有实际触及日常资料的证据。
- 当前未记录原生返回的实际字符串，也没有异常栈以外的更细取证。不能归因为字符编码、路径重定向、API ABI 或沙箱中的某一项；这些均未确认。
- 托管测量工具编译与 6 项纯统计／候选顺序契约通过，不能替代原生加载、部署、ABI、隔离目录及语义验证。

高级模型接手时先核对 1.13.1 官方 C ABI、traits 生命周期／路径契约和实际返回值；保持创建会话前的严格隔离保护。只有证明目录确实独立后才运行合成输入，不能以屏蔽校验或换到未检查目录获得“性能数据”。
