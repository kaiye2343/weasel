# 原生有界性能探针

## 当前范围：C1a 核心

`include/WeaselPerformance.h` 提供 C++17 标准库实现的采样核心。默认关闭，按链接后的 PE 端点各自持有实例，不是跨进程计时器。本批没有接入真实输入调用点，也没有为日常服务增加启用方式；不能据此宣称已经定位或改善输入延迟。

计时使用 `steady_clock` 单调纳秒时间；Windows 标准库使用自身单调时钟实现。测试可显式传入假时钟，不读取环境、用户配置或目录，不启动后台线程。

### 生命周期

1. 隔离调用者准备 `Options`，明确实际可测阶段，再调用 `Probe::Start`。默认 30 秒，只接受 1—120 秒。
2. `Scope` 或 `Begin`／`End` 记录阶段耗时；`Add` 记录固定类别计数。关闭时不读取时钟、不获取采样互斥锁、不分配样本。
3. 每阶段最多 65,536 个样本；启动时为可用阶段预留容量。满后记录丢弃数量，不扩容，其他阶段可继续采样。超过截止后停止收集；不创建后台定时器。
4. 控制路径调用 `Finish` 停止、汇总并释放样本容量，再显式调用 `WriteReport` 写入调用者提供的流。没有逐键磁盘写入。

重复启动必须先结束原会话，达到截止也不允许跳过结束步骤。旧 Token 通过会话代次隔离。报告构造失败时，在同一持锁区域清理失败会话，保留调用者旧报告；不会解锁后无条件清除后来启动的新会话。

### 报告契约

JSON `schemaVersion=1`，字段包括源码号、PID、位宽、实际后端、DPI、合成场景标识、请求时长、实际采样时长、阶段和计数。来源只接受完整 40 位十六进制提交号或 `unknown`；场景标识只接受 1—64 位 ASCII 字母、数字、点、下划线和连字符，不接受私人路径或输入正文。

阶段名固定：

- `core_process_key`
- `context_response`
- `ipc_roundtrip`
- `layout`
- `resource_prepare`
- `paint_submit`
- `present_complete`

各阶段包含 `status`、样本量、丢弃量和 P50／P95／最大毫秒数。百分位采用最近秩。显式未测阶段为 `unavailable`，三个时间值为 `null`；可测但没有样本为 `empty`，时间值为 0。呈现提交不能冒充屏幕呈现完成。

计数类别为 `file_metadata`、`digest_read`、`decode`、`scale`、`background_rebuild`、`frame_submit`；累加采用饱和运算，不回绕。它们在本批仅用于核心合成测试，尚未代表真实输入链路的操作次数。

序列化不修改调用者流的语言或精度设置，JSON 数字固定使用小数点。报告不包含按键正文、候选文字、词典内容或个人路径。`ipc_roundtrip` 接入后会包含服务端时间，不能直接与核心时间相加；本批尚未采集该链路。

## 验证与限制

`tests/native_performance_probe_test.cpp` 使用合成时钟，覆盖九组回归：默认关闭与零分配、参数校验、已知百分位、容量与复用、截止与逆行、旧 Token／非法索引、多线程、分配故障恢复、JSON／语言设置。分配故障只由测试 exe 的 `thread_local` 一次性开关及替换 `new/delete` 实现，不加入产品接口。

在已配置 MSVC 开发环境的隔离目录运行：

```powershell
./tools/test-native-performance-probe.ps1 -Architecture x64
# 切换为 x86 编译环境后：
./tools/test-native-performance-probe.ps1 -Architecture Win32
```

脚本使用 `/O2 /W4 /WX` 编译，运行无窗口夹具，并真实解析报告 JSON，校验固定字段、阶段、空值及界限。另编译先包含 `windows.h` 的探针调用，验证 Windows 宏兼容。两架构合成报告写入 `output/archives/performance-core-*.json`，测试中使用的 PID／后端等为合成元数据，不表示真实 GPU 或输入测量。

本机没有 MSVC 编译器；提交前仅完成静态审查、脚本和工作流语法检查。实际两架构回归等待 Actions。后续 C1b／C1c 才接入真实阶段、隔离启用入口和启用／关闭开销对照；本批不修改 IPC、UIStyle 或 librime，不开启日常输入法采样。
