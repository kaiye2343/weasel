# 皮肤文件访问采集（开发诊断工具）

用于区分“图片路径正确但宿主读取失败”和“文件打开成功、随后解码失败”。它不修改输入法、皮肤、用户词典或宿主进程，不使用注入钩子，也不作为常驻组件。桌面复现由用户完成。

## 使用

下载 `Skin file access diagnostics` 工作流产物中的独立工具。用 Windows 正常的管理员终端运行；本工具不会自动提权、添加用户组或修改安全设置。

```powershell
# PID 取任务管理器“详细信息”中发生问题的应用；应用重启后需重新确认。
# 输出文件必须不存在，目录必须已存在；建议放在本仓库忽略的 .tools 下。
.\SkinFileTrace.exe --pids 4844 --assets shorekeeper-watermelon.png,shorekeeper-wing-reference.png,shorekeeper-wordmark.png --seconds 30 --output E:\code\输入法\.tools\skin-file-access.jsonl
```

看到 `READY` 后，在目标应用中复现图片消失，持续修改拼音数秒。到时自动停止，Ctrl+C 也可停止。最多 8 个 PID、8 个 PNG 文件名、60 秒；多个 PID 用逗号隔开。不要在采集中重启目标应用或替换 PID。

采集使用微软 [TraceEvent](https://github.com/microsoft/perfview) 的实时 ETW 接口，启用 `Microsoft-Windows-Kernel-File` 的打开和操作结束事件。控制接口参考 [EnableTraceEx2](https://learn.microsoft.com/en-us/windows/win32/api/evntrace/nf-evntrace-enabletraceex2)。

## 输出与边界

- `open`：实际内核路径、发起 PID／线程、IRP、原始打开选项和共享标记。NT 原生选项以十六进制保留，不误解释为 Win32 文件属性。
- `complete`：按 IRP 配对的 `NTSTATUS`，不是 `GetLastError()`。如 `0xC000003A` 表示 `STATUS_OBJECT_PATH_NOT_FOUND`。
- `summary`：打开、配对、失败、丢事件、无法解码及未配对数量。空日志或未配对不能解释为访问成功；`usable=false` 必须保留这一限制。
- Windows 某些失败可能发生在该提供程序发事件之前；没有捕获到请求时，仍需其他诊断，不能宣布排除文件访问问题。
- 不写 ETL，不保存全局事件流、文件内容、按键或调用栈。PID 过滤包含 System（4），以接收异步操作结束；只有已匹配目标 PID 与 PNG 文件名的请求才会保存路径，未匹配事件立即丢弃。
- 输出含指定 PNG 的真实路径，应按私人诊断资料保存，不上传到公开仓库。重启进程后的 PID 复用不在捕获保证范围内。
- 正常退出会停止本工具唯一会话；若工具被强行终止，管理员可用 `logman stop "READY 中的 RimeSkinFileTrace-会话名" -ets` 清理该会话。不要停止其他会话。

## 验证

```powershell
dotnet run --project tools/SkinFileTrace -c Release -- --self-test
# 以下只在管理员环境或 CI 中运行：创建私有临时文件触发成功／缺失路径访问，无窗口操作。
dotnet run --project tools/SkinFileTrace -c Release -- --trace-self-test --output .tools/skin-file-trace-self-test.jsonl
```

自测不等于 EverythingToolbar 的故障复现；只有用户复现的请求和错误能用于判定该宿主的原因。此工具没有改变小狼毫的读取或绘制实现。
