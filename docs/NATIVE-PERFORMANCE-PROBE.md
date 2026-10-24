# 原生有界性能探针

## C1a 核心

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

本机没有 MSVC 编译器。`73133f5` 的[完整 CI 40064217507](https://github.com/kaiye2343/weasel/actions/runs/40064217507) 已通过：Win32／x64 各九组回归、Windows 宏兼容及实际 JSON 校验成功，11 对符号和完整安装包门槛通过，生产警告／错误均为 0。产物 `0.17.4.146.73133f5` 未安装；核心仅合成验证，不表示实际输入提速。

## C1b 真实调用点与隔离预览

**2026-10-24 repair 更新：** 原始非符号程序已取得执行地址 0 的异常指令，返回地址落在 `Probe::Start` 的 `clock_()`；原命名空间 inline 实例在该构建中没有可用构造状态。统一改用 `GetProcessProbe()` 首次使用构造入口。生产 `/Zc:threadSafeInit-` 保持不变，由常量初始化的原子指针与 `std::call_once` 显式保护构造，已构造端点只读指针快速路径；没有改变 Begin／End／Add 内部计时逻辑。两单元、屏障首次并发及四种编译模式的双架构回归已由完整 CI 40090582105 验证通过，原失败编译条件和优化模式均不再 AV。[证据与范围](REPAIR-EXECUTION-20261024.md)。

新增调用点均默认关闭，不通过环境变量或逐键读配置启用：

| 阶段 | 实际边界 |
|---|---|
| `core_process_key` | 唯一 `rime_api->process_key` 调用及其参数求值，Vim、响应和 UI 更新在外 |
| `context_response` | `_Respond` 的读取、构造、序列化及原 `eat` 回调 |
| `ipc_roundtrip` | `_SendMessage` 中构造请求到 `channel.Transact` 返回／抛出 |
| `layout` | 字体资源与文字布局，结束于 `_ResizeWindow` 前 |
| `resource_prepare` | 更新检测与装饰几何、实际缩放、按需背景加载等独立准备操作 |
| `paint_submit` | `DoPaint` 的 CPU 组帧、原生提交及清理 |
| `present_complete` | 仍不可测，未伪造完成信号 |

资源样本对应多种准备操作，并非固定每帧一条；绘制时间包含其中部分资源工作，IPC 也包含服务处理，阶段数值不能直接相加。`file_metadata` 只数皮肤更新检测的 `last_write_time`，不是全进程所有文件 API；`digest_read` 数真实 SHA256 计算尝试，含源、发布目标及暂存验证。解码、缩放、背景重建计尝试，帧提交仅计 `UpdateLayeredWindow` 成功。

原生预览新增 `--performance-probe-test`，只处理合成候选及独立皮肤资料；`--probe-duration` 接受 1—120 秒，`--probe-revision` 只接受完整提交号或 `unknown`。缺值、无效值或仅在普通预览中传递这些采样参数时返回 2，不默默进入普通界面。

`tools/test-native-performance-preview.ps1` 限隔离 CI，使用纯色与 PNG 示例各运行三组“关闭—开启—开启—关闭”，每轮 200 帧，核对真实 PID、源码号、位宽、后端和单一显示器 DPI。只启用该端点真实可测的布局、资源和绘制阶段；核心、响应、IPC 和呈现完成均为 `unavailable`。超过采样期限的启用轮直接失败，不能混入开销对照。

像素 hash 在计时和采样停止后，通过独立强制重绘取得。报告 `finalRedrawEquivalent` 仅证明各轮相同最终候选状态的重绘尺寸／像素相同，**没有逐帧截取或证明采样期间的全部帧相同**。取不到 hash 时失败，不能用多个缺失值判等。无需为了诊断插桩增加完整帧抓取架构。

报告归档为 `performance-preview-*.json` 和 `performance-probe-overhead.csv`，保留每组数据；不对共享 runner 的一次绝对毫秒数设门槛。实际响应代码另在已有假 Rime／真实客户端解析夹具中启用采样，验证成功、空响应与失败回调共 15 次均结束计时；这不代表真实 librime 算法时间。

C1b 的完整 CI 在隔离响应夹具运行时失败，开销结果未验收；按用户要求停止相关修复尝试，交接给高级模型，详见[访问违例问题单](issues/2026-10-24-response-fixture-access-violation.md)。真实服务／IPC 的隔离启用入口、核心测量和冷缓存场景继续留待 C1c。尚未据此实施缓存优化或宣称性能收益，不修改 IPC、UIStyle、算法或日常配置。
