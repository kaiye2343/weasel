# repair 实施与验证记录

依据用户明确指令“根据 repair 修复”，执行 `docs/issues/2026-10-24-bug-repair-guidance-plan.md`。这次授权针对已交接的 C1b 与 D1 问题；新的独立问题仍建单交接。不安装、不改日常资料、不操作桌面输入框。

## C1b-A：保持失败条件的取证

- `a41aa12`／[40085295213](https://github.com/kaiye2343/weasel/actions/runs/40085295213)：原始 `/MT`、默认优化构建仍 AV；加入 `/Zi`、`/DEBUG` 和测试内异常过滤器的独立符号版却通过。因此没有把符号版通过当作修复。
- `b6e89b7`／[40086671941](https://github.com/kaiye2343/weasel/actions/runs/40086671941)：独立测试调试器仅启动原始 response.exe，使用调试事件线程的真实 CONTEXT，不附加日常进程、不修改目标代码；保存原 EXE、MAP、反汇编及日志。原程序再次返回 `0xc0000005`。
- 工具链日志为 MSVC `19.44.35229.0`，Windows 2022、x64。没有把结论泛化成所有 MSVC 版本的已知缺陷。

原始调试输出：

```text
image-base=0x7ff790970000 probe-rva=0xdff60
code=0xc0000005 instruction=0 operation=8 target=0
probe-bytes=528 nonzero-bytes=4
rcx=0x7ff790a4ff60
stack-top=0x7ff7909929c6 possible-return-rva=0x229c6
```

MAP 中 `Probe::Start` 地址为 `0x1400228d0`，探针实例为 `0x1400dff60`。原 EXE 反汇编：

```text
00000001400229C3: FF 50 58   call qword ptr [rax+58h]
00000001400229C6: ...       ; actual return address
```

该指令位于参数校验与加锁之后、读取采样时长之前，对应 `clock_()`。真实异常为执行空函数指针，而非已证明的 Rime API、图片解码或分配故障。原 MAP 未保留 Probe 构造函数，独立符号版有构造函数并通过；当前非局部 inline 实例在失败构建中没有可用的构造状态。这支持 repair 的“实例初始化／生命周期”分支，不以更多猜测补丁替代证据。

原始 artifact：`12557223502`，下载证据位于本机忽略目录 `.tools/dsh-plan-20261023/evidence-40086671941`。跨机器请从对应 CI 获取失败附件，不把本机目录作为产品依赖。

## C1b-B/C：最小修复与实际编译条件

- 移除命名空间 `inline Probe processProbe`，统一调用 `GetProcessProbe()`；不改 Probe 的采样、容量、并发清理、报告或计时边界。
- 生产工程存在 `/Zc:threadSafeInit-`。因此 getter 使用常量初始化的原子指针作快速读取，首次使用由 `std::call_once` 保护局部静态 Probe 的构造，完成后 release 发布；正常调用 acquire 读取已构造实例。没有全局放宽编译选项，也不依赖编译器的局部静态初始化锁。
- 同一链接 PE 的翻译单元共享实例，不跨进程、也不把不同 DLL 当作一个端点。退出时仍按静态对象生命周期释放；不增加常驻线程、文件访问或退出 hook。
- 新增两编译单元回归：八线程通过 ready/go 屏障后首次访问，同地址、默认关闭、真实时钟 Start、两单元各一条 Scope、Finish 两条样本。
- Win32／x64 各测默认、`/O2`、`/Zc:threadSafeInit-`、两者同时启用四种模式。响应夹具在两架构各测默认及 `/O2`，继续保留 current/baseline 语义、失败回调与 15 次样本检查。
- 清理临时阶段日志、异常过滤器与独立调试器，原始失败证据仍留在 Git 历史及 CI 附件；不把调试代码带入产品。

原 C1b DSH 会话被已有写入句柄占用，未清锁、抢占或更换会话绕过该状态；此项修复与生命周期测试由 Codex 实施。相关工具限制独立记录。

`fab86f9` 为修复提交；`5991770` 的[完整 CI 40090582105](https://github.com/kaiye2343/weasel/actions/runs/40090582105) 已成功：Win32／x64 各四种编译模式的首次访问／跨单元回归、默认及 `/O2` 响应／baseline 语义、15 次样本、既有负控制与所有完整包门槛均通过。原失败条件不再 AV。完整产物 `0.17.4.156.5991770`、artifact `12559385772` 已上传，未安装。

同一 CI 的六组隔离预览 ABBA：中性皮肤启用／关闭 wall-time 比为 1.00～1.02，示例图片皮肤为 1.00～1.01（日志四舍五入到两位）；每轮 200 帧、DPI 96。停止采样后的最终重绘一致。此项验证有界探针的运行开销和终态，不证明每帧一致、所有 DPI 的开销相同或输入算法已提速；未据共享 runner 小幅波动宣称收益。

## D1-A/C：区分编辑器与真实选词

- 采用 repair 建议：设计器候选点击继续只选择预览高亮，保留原有编辑边界与图片选择职责。页面明确说明实际选词区域以原生预览为准，没有悄悄把编辑点击改成原生鼠标模拟。
- 生产矩形命中采用原有左上包含、右下排除规则；实际还包含高亮留白、内容原点、DPI、重排偏移及事件状态。不同鼠标事件保留各自生产顺序，不人为统一成“首个命中”。
- DSH 会话 `session-fb25d203-7a50-4a62-a266-c8258e352db2`，已核实路由 WorkBuddy Global Flash、`workspace-write / ask`，仅扩充提取真实 `OnMouseMove` 的无 HWND 测试。Codex 审查实际 diff，确认三 DPI × 十三个边界点、原点／重排／留白、两类悬停重叠语义与空页；没有改生产事件。
- 这些候选矩形是受控模拟数据；不是 `HorizontalLayout` 文字测量结果。真实布局事实与点击矩阵随后按下节补充，执行结果与本段首次悬停 CI 分开记录。
- 本地 59 项逻辑、32 项隔离 headless Edge 浏览器测试通过；网页无外部请求。原生事件回归待 CI。

Node 的 `--test` 子进程 `spawn EPERM` 是本机受限测试启动条件，未修改机器权限。直接执行现有共享夹具文件已通过两项；这与隔离 CI 的 `node --test` 成功分别记录，不能说本机权限问题已修复。

## D1-B/C：原生事实与完整事件夹具

- 新增独立预览命令 `--layout-facts <合成夹具> <报告>`，在创建任何 HWND 之前执行。仅初始化本进程 COM／GDI+、内存 DC 和 DirectWrite，不加载日常皮肤、配置或词库。
- 同一原始候选 JSON 分别送入生产 `HorizontalLayout::DoLayout` 和网页 `SkinGeometry.layout`。固定测量子类只替换原生文字测量，不复写布局算法、不接收网页矩形。八场景 × 三 DPI，固定测量与真实 DirectWrite 各 24 组。
- JSON 分别输出布局矩形、序号／文字／注音矩形、高亮包围矩形、最终命中矩形及加入窗口原点后的坐标；同时记录 DPI、留白和重排偏移。`textBottom` 是布局文字框底边，不冒充字体字形基线。报告中的高亮包围矩形也不声称等于圆角裁剪后的逐像素形状。
- 固定测量与网页只允许不超过 1 个物理像素的取整差；真实字体结果单列为原生事实，不把不同测量来源的大小写成一致。空注音判断面积为零，保留生产 padding 对空矩形原点的平移。
- 原 DSH 会话续做仅扩展 `upstream_hover_test.cpp`。Codex 审查真实 diff，并接入提取的生产按下／松开方法；定时器、截图、重绘和窗口销毁使用无副作用测试替身，不操作鼠标或系统窗口。覆盖按下／松开不同候选、半开边界、DPI、重叠、隐藏／空页与翻页优先路径。原错误原点负控制保留，另加右／下闭边界负控制。
- 当前本地 JS／PowerShell 语法、59 项设计器逻辑和两项共享数据检查通过。原生事实与新点击夹具实际执行等待本批完整 CI；不提前宣称 D1 所有字体／所有交互或日常实机一致。

## 原生取证 API 依据

使用微软原生调试接口的[调试事件循环](https://learn.microsoft.com/en-us/windows/win32/debug/writing-the-debugger-s-main-loop)、[StackWalk64](https://learn.microsoft.com/en-us/windows/win32/api/dbghelp/nf-dbghelp-stackwalk64) 和 [SymFromAddr](https://learn.microsoft.com/en-us/windows/win32/api/dbghelp/nf-dbghelp-symfromaddr)。符号版改变复现条件后，改由独立进程读取原目标异常，未声称捕获到了一个不存在的符号栈。
