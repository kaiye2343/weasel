# 开源输入法皮肤实现调研与上游改动说明

查证日期：2026-10-21。对照本项目：`168e896d347cd7d12c5796c829e1f2c602c704bb`。

后续进展（2026-10-22）：用户已授权实施第 7 节建议，部署器 CLI 已移植并适配，见 [实现与验证说明](DEPLOYER-CLI.md)。下文“尚未移植”保留为调研时状态；渲染迁移在独立分支推进，未替换日常分支默认后端。

## 1. 结论

建议继续使用**原生候选窗、数据型皮肤包和独立网页设计器**，先补齐几何、定位、资源生命周期与预览约定，再决定是否迁移绘制后端。

- **最值得借鉴 Fcitx5 Classic UI**：文字、内容留白、九宫格、装饰裁剪和点击范围分别建模；图片作为主题资源复用。
- **上方候选注音可参考 Trime**：注音与候选文字是分别排版的元素；主题资源缓存与主题切换有明确生命周期。
- **网页设计器可参考 Fcitx5 Android 的独立设计器**；但其核对版本主要编辑配色，不能直接提供我们需要的自由图片组合。
- **fcitx5-webview 是真实的网页候选窗实现**，能缩小网页预览与运行时的绘制差异。但它不能直接替换 Windows TSF 前端，也没有证据证明迁移后按键延迟更低。
- **外置图片跟随窗口仍有双窗口同步成本**。Rime Skin Overlay 的新版有真 Alpha 和多图层，不能再概括成“只能色键透明”；不过它仍不能统一小狼毫文字框和外挂图片的布局生命周期。
- **上游 DirectComposition 与背景图 PR 是两件事**。前者是绘制后端，后者截至查证日仍未合并；二者都不直接提供本项目完整皮肤格式。

本轮为文档与源码调研，没有新增性能实测、安装构建或操作用户桌面。下文“建议”均未在本轮实施。

## 2. 取样与证据范围

以项目自身文档和固定提交源码为依据。下表是查证时的主线快照，**不等同于各项目的稳定发行版本**。不以 Issue、功能请求或宣传截图代替实现证据。

| 项目 | 核对版本 | 用途 |
|---|---|---|
| 官方 Weasel | `82b50c9e9b9ee0bd31357db6f25ddc804546c7f7`；另对照 0.17.4 | Windows 原生候选窗、DComp、TSF 所有权、部署器 |
| Fcitx5 | `456ff901705de75a979fa4b75b4be35edc73aeb9` | Linux Classic UI 的主题几何、九宫格与图片复用 |
| fcitx5-webview | `c509e24c1c87e96edd215b67f3993f000ecf8c31` | 网页候选窗与共用预览入口 |
| Trime | develop `12ffd16fe03537b6d0ed9716c71cbc33608c2a33` | Android 候选注释排版、图片缓存与主题切换 |
| Fcitx5 Android | `eb5b50216e70484a009933d9519db76005f1faf3` | 主题管理、背景图片、包导入导出 |
| Fcitx5 Android theme-designer | `8ee94cbc84ba8e8e3095220d3aa2d3b866b29151` | 独立网页编辑与导出闭环 |
| Mozc | `f183d4d3a7ba139d7cb2252c05f608e9f8266aef` | Windows 独立候选绘制进程的架构参考 |
| Rime Skin Overlay | main `a8fca3741e26d3ed80ebfcc7de749d94ea65b0d6` | Windows 外挂多图层、事件跟随、真 Alpha |

本项目探索先使用 CodeGraph；原生部分索引覆盖有限，随后核对具体生产文件。外部项目采用固定提交的源码快照，没有执行它们的安装程序。

DSH 辅助路径也做了核查：旧 Desktop 会话不可用且目录不符，未向其派发；`dsh-agent` 另外确实提供一次性 headless 短任务脚本。本机 headless profile 只有配置骨架，尚未安装对应依赖、未配置模型，因此本轮未运行 DSH，未采纳任何 DSH 自述结论。

## 3. 能力比较

“未核实”表示本轮没有找到足够实现证据，不表示项目绝对无法扩展。

| 实现 | 背景与装饰 | 候选注释 | 预览关系 | 与本项目的适配判断 |
|---|---|---|---|---|
| Weasel 0.17.4 | 配色、字体、间距、圆角等原生样式；不含我们的通用图片包 | 原有注释；不含本分支统一上方注音扩展 | 无本项目的网页设计器 | 可作为稳定原生基础，图片与上方注音仍需扩展 |
| Fcitx5 Classic UI | 九宫格背景、高亮背景、锚定 overlay、裁剪与点击留白 | 文本元素由候选 UI 排版；未核实可直接套用我们的双行规则 | 本轮未核实官方自由图层网页设计器 | 最适合参考几何和主题资源生命周期；不能直接移植 Linux 窗口代码 |
| fcitx5-webview | CSS 样式、背景图、自定义 CSS、浏览器合成 | 有 label/text/comment 数据接口 | 预览程序调用同一候选窗类与页面 | 预览一致性思路值得借鉴；Windows 宿主适配与性能需另验 |
| Trime | YAML 主题、颜色或图片资源、Android 九宫格 Drawable | TOP／RIGHT／OVERLAY 排版分支 | Android 原生布局；不是本项目的 Windows 网页预览 | 注释布局和资源 generation 可参考，不能复制成 Windows UI |
| Fcitx5 Android | 配色、自定义背景图片、主题 ZIP | 使用 Android 候选控件 | 独立网页主要模拟键盘配色 | 可参考管理与导入闭环，不是自由桌面角色皮肤框架 |
| Mozc | 本轮主要确认 renderer 架构 | 由 renderer 接收候选数据绘制 | 本轮未核实通用图片皮肤设计器 | 可参考协议与绘制进程职责，不作为现成皮肤系统推荐 |
| Rime Skin Overlay v2.0 源码 | 多图片层、动画与透明分层窗口 | 仍由原输入法绘制文字 | 有自身图层预览，不能代替小狼毫文字测量 | 不建议回到外挂作为本产品主要实现 |

## 4. 实现细节与可借鉴部分

### 4.1 Fcitx5 Classic UI：把几何边界分清楚

官方说明将文字尺寸、Text Margin 与 Content Margin 分开计算。背景九宫格保持四角，分别拉伸边和中心；高亮背景也使用同类规则。[官方主题文档](https://fcitx-im.org/wiki/Fcitx_5_Theme)

源码还提供 `Overlay`、`Gravity`、X/Y 偏移、`OverlayClipMargin`、`HideOverlayIfOversize` 和 `HighlightClickMargin`。因此，装饰、裁剪和点击并非一个矩形。它的 overlay 会受到裁剪区域约束，不能据此宣称能无条件画出窗口之外。[配置定义](https://github.com/fcitx/fcitx5/blob/456ff901705de75a979fa4b75b4be35edc73aeb9/src/ui/classic/theme.h#L120)

`Theme::loadBackground` 复用主题图片表；`ThemeImage` 持有九片素材及不同 scale 的资源，绘制选择适合的 scale；切换主题的 `reset()` 清空图片表。这支持“资源按生命周期加载、绘制复用”的思路，但不是预先生成所有可能宽高的完整截图。[加载与绘制源码](https://github.com/fcitx/fcitx5/blob/456ff901705de75a979fa4b75b4be35edc73aeb9/src/ui/classic/theme.cpp#L869)

**对我们的建议：**明确区分文字内容框、可见皮肤框、包含图片的画布、选词命中区，以及相对输入光标的定位锚点。它们可以相关，但不能共用一个边界代替所有含义。

### 4.2 Trime：注音位置与主题状态分别处理

当前候选控件从 `item.comment` 取注释，分别创建注释与候选文字视图；`commentPosition` 包含右侧、上方和叠加分支。它证明“已有候选注释移到上面”无需另外开发拼音推断器；没有注释也不应猜多音字读音。[候选布局源码](https://github.com/osfans/trime/blob/12ffd16fe03537b6d0ed9716c71cbc33608c2a33/app/src/main/java/com/osfans/trime/ime/candidates/CandidateItemUi.kt#L88)

`ColorManager` 使用按路径索引的 Bitmap LRU，按图片实际字节数计费，预算取进程最大内存的八分之一；`imageDrawable` 复用解码结果，识别 `.9.png`。`attachTheme` 清空 Bitmap 缓存，颜色方案另有 generation 供依赖者重新解析。[资源实现](https://github.com/osfans/trime/blob/12ffd16fe03537b6d0ed9716c71cbc33608c2a33/app/src/main/java/com/osfans/trime/data/theme/ColorManager.kt#L46)

**对我们的建议：**沿用 Windows 现有明确的 64 MiB 预算，借鉴主题版本与缓存失效关系；不照搬 Android 的内存比例。双行高度应由整行候选共同确定，部分无注音时汉字仍共享基线。此项是我们的布局要求，不能仅凭 Trime 存在 TOP 分支就宣称已验证相同效果。

### 4.3 Fcitx5 Android：管理闭环与独立网页设计器

`ThemeManager` 有主题枚举、保存、同名替换、删除、活动主题重新选择和变更通知。`ThemeFilesManager` 将主题 JSON、裁剪图片和原图片打包为 ZIP；导出时去除设备内部完整路径，导入先解到临时目录并返回是否新建的信息。[主题管理](https://github.com/fcitx5-android/fcitx5-android/blob/eb5b50216e70484a009933d9519db76005f1faf3/app/src/main/java/org/fcitx/fcitx5/android/data/theme/ThemeManager.kt#L99)、[文件导入导出](https://github.com/fcitx5-android/fcitx5-android/blob/eb5b50216e70484a009933d9519db76005f1faf3/app/src/main/java/org/fcitx/fcitx5/android/data/theme/ThemeFilesManager.kt#L83)

独立 `theme-designer` 确实存在。核对版本可以读取 JSON／ZIP，以 Vue 模拟键盘，导出 ZIP 内的主题 JSON。其导出函数没有打包我们这种任意多图片图层；不可将“输入法支持背景图”与“网页设计器支持完整图片创作”混为一谈。[网页实际导入导出](https://github.com/fcitx5-android/theme-designer/blob/8ee94cbc84ba8e8e3095220d3aa2d3b866b29151/src/App.vue#L34)

**对我们的建议：**保留设计器与输入设置中心分工。主题移除、同标识版本冲突、覆盖确认、活动主题回退应成为完整产品行为；不直接复制其文件覆盖逻辑替代本项目更严格的原子导入和可恢复副本机制。

### 4.4 fcitx5-webview：共用真实页面比“画得像”更可靠

项目用于 fcitx5-macos／fcitx5-js 的网页候选窗。接口传递候选文本、序号、注释和动作；`customize.ts` 将样式映射为 CSS 变量，提供背景图及用户 CSS 入口。[项目说明](https://github.com/fcitx-contrib/fcitx5-webview)、[样式实现](https://github.com/fcitx-contrib/fcitx5-webview/blob/c509e24c1c87e96edd215b67f3993f000ecf8c31/page/customize.ts)

其原生预览直接创建 `WebviewCandidateWindow` 并喂入模拟候选；宿主接收网页尺寸和锚点回调，过期 epoch 的布局回报会被丢弃。这比独立复制一套视觉样式更能保持同一路径。[预览程序](https://github.com/fcitx-contrib/fcitx5-webview/blob/c509e24c1c87e96edd215b67f3993f000ecf8c31/preview/preview.cpp)、[宿主桥接](https://github.com/fcitx-contrib/fcitx5-webview/blob/c509e24c1c87e96edd215b67f3993f000ecf8c31/src/webview_candidate_window.cpp#L59)

**对我们的建议：**网页用于编辑，Windows 原生预览用于最终几何验收。两者共用规范化后的场景数据、偏好、框体和图片锚点；若字体度量不同就明确显示差异。未来可以另做 WebView 后端试验，但本轮没有 Windows 宿主兼容、焦点、内存、启动及端到端延迟证据，不建议立即重写。

### 4.5 Mozc：分离候选数据与绘制职责

Mozc 将候选 GUI 放在 renderer 进程，客户端经 IPC 发送协议定义的数据；Windows 有独立入口及窗口管理实现。[renderer 文档](https://github.com/google/mozc/blob/f183d4d3a7ba139d7cb2252c05f608e9f8266aef/src/renderer/README.md)、[Windows 窗口管理](https://github.com/google/mozc/blob/f183d4d3a7ba139d7cb2252c05f608e9f8266aef/src/renderer/win32/window_manager.cc)

这是职责分离的参考，**不是本轮发现的成熟角色图片皮肤系统**。绘制进程集中管理资源有工程价值，也会增加协议、进程和故障恢复工作；不能仅因其独立进程就判断更快、更稳定或能接管系统候选 UI。

### 4.6 Rime Skin Overlay：新版功能应准确描述

核对 main 源码的 `VERSION` 为 `v2.0`：有多图层档案、WinEventHook 候选窗口跟随、帧缓存与 `LayeredRenderer`。真 Alpha 路径使用预乘 BGRA 和 `UpdateLayeredWindow`；不能沿用旧 README 缓存页面，把它描述为仅支持二值透明。[固定版本源码](https://github.com/MiHjy12138/Rime-skin-overlay/blob/a8fca3741e26d3ed80ebfcc7de749d94ea65b0d6/rime_char_overlay.py#L2336)

**对我们的建议：**可以参考事件去重、图层档案和缓存组织。用户之前遇到黑闪与位置脱节是本机使用事实，但本轮没有重新复现，也不推断该版本在所有环境都会失败。作为本产品主路线，统一原生画布仍更便于让候选文字、图片、屏幕边缘和命中坐标一起更新。

## 5. 对照我们当前的问题

### 5.1 图片缓存已经存在，需要补齐的是失效与状态证据

现有 `WeaselPanel` 已有缩放 PNG 缓存与固定背景合成缓存；不能再把“新增缓存”当作全新的修复方案。布局尺寸、DPI、皮肤资源及外观状态变化时，缓存必须正确失效；切换外观还需避免新布局搭配旧资源。

用户这次偶发不加载仍未定位。当前保留的 10 月 21 日渲染日志没有加载／提交失败，不能把它直接定性为缓存故障。历史的路径视图问题已有不同证据和迁移处理，不应自动套到这次故障。

建议把诊断结果拆成：

1. 宿主是否允许输入法显示自身候选窗；
2. 生效的皮肤与资源 revision；
3. 解码／缩放／背景缓存命中及失效原因；
4. 内容框、画布、最终屏幕矩形和裁剪；
5. 绘制是否执行、窗口是否显示及提交是否成功。

日志记录状态变化与错误，不逐帧输出大段内容，也不记录用户正在输入的完整句子。

### 5.2 开始菜单／任务栏搜索：先确定候选窗归属

Microsoft 明确规定，`BeginUIElement` 返回 `pbShow=FALSE` 时，应用可能自行绘制 UI，文本服务不显示自己的对应 UI。本项目 `CandidateList::StartUI` 也据此决定是否创建候选窗口。[Microsoft API 约定](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfuielementmgr-beginuielement)

因此，开始菜单里无角色图片可能与宿主接管有关，也可能是其他生命周期问题；**此机现场尚未证明是哪一种**。如果没有本体候选窗，增加图片缓存或更换 D2D 都不会自动让系统绘制我们的装饰。不建议先用挂钩强制覆盖系统 UI。

### 5.3 预览与实际位置不同：不只比较截图外观

同一个旧皮肤包导入后仍可能因有效字号、密度、候选内容长度、有无注音、DPI 和字体测量不同得到不同框体尺寸；以框角锚定的图片就会随之移动。这些是需要排查的变量，不是已经确认的唯一原因。

建议设计器增加“按皮肤默认／模拟当前显示偏好”明确模式，并可导入不含个人词库的显示偏好。比较时使用同一候选数据、同一变体、同一坐标单位和同一可见框体基准。原生预览应输出实际计算矩形，供自动化几何对照；网页不应只用固定示例宽度承诺实机位置。

### 5.4 候选窗遮挡输入文字：定位必须考虑装饰

现有 `SkinGeometry` 会计算图片并集并平移到窗口画布，但 `_RepositionWindow` 的光标定位与装饰外扩避让仍需一起审查。只有文字框不压住光标，不代表上方蝴蝶或右侧角色不会遮挡。

建议在设置中心提供**用户级候选窗间距／偏移与恢复默认**，皮肤只声明自身几何。先按光标矩形选择上下方，再用完整装饰边界与屏幕工作区校正；装饰仍不能扩大选词点击区域。预览显示模拟输入行、光标、候选框及全部图片，允许用户检查偏移效果。屏幕不足时的图片裁剪或位置翻转应明确规则，不以意外消失作为退让机制。

## 6. 上游 DirectComposition 是否值得迁移

官方当前后端具备透明合成与设备资源基础，但没有本项目的完整图片层、九宫格、上方注音、外扩画布与偏好优先级。迁移仍需适配，而非直接合并就获得完整皮肤。

核对 `DeviceResources::EnsureInitialized` 后，官方代码先尝试 **WARP 软件设备**，失败再试硬件。因此“换 DComp 就默认 GPU 加速、更快”并不成立。[固定上游源码](https://github.com/rime/weasel/blob/82b50c9e9b9ee0bd31357db6f25ddc804546c7f7/WeaselUI/d2d.cpp)

既有受控合成微基准的 GDI 与 WARP/D2D+CPU 回读路径不同于完整 DComp 呈现，不能据其判定后者日常更慢或更快。长串输入还包含 Rime 核心查询，不能归结为皮肤。详细限制与原始数据见 [既有后端评估](RENDERER-EVALUATION.md) 和 [官方／改版性能对照](PERFORMANCE-20261018-OFFICIAL-COMPARISON.md)。

建议先完成上述几何与生命周期修正，再建立同一内容、同一图片、同一字号与同一呈现条件的完整后端对照。迁移时保留现有回退，补齐设备丢失、资源重建、DPI、跨屏和鼠标命中验证。

## 7. 注释说明：把部署器改成控制台程序意味着什么

用户标注的提交是 [`2c587279e4f341a447bebefd94d6519847d2cced`](https://github.com/rime/weasel/commit/2c587279e4f341a447bebefd94d6519847d2cced)。

### 实际改动

- `WeaselDeployer.exe` 从 Windows GUI 子系统改为 Console 子系统，入口从 `_tWinMain` 改为 `wmain(argc, argv)`。
- 加入 `/dict list`、`backup`、`restore`、`export`、`import`，通过 Rime 官方 levers 接口执行词典操作。
- 原有图形对话框仍在；我们的 WPF 设置中心不会因此变成命令行。
- 非词典 CLI 路径调用 `DetachGuiConsole()`：对自身新建的控制台尝试隐藏，然后脱离控制台；上游调用方也增加隐藏启动处理。因此不能说“合并后一定闪黑窗”。[部署器实际入口](https://github.com/rime/weasel/blob/82b50c9e9b9ee0bd31357db6f25ddc804546c7f7/WeaselDeployer/WeaselDeployer.cpp)

### 收益与仍需验证的地方

词典 CLI 方便设置中心读取列表、发起快照备份、恢复和导入导出；这与图片绘制和缓存没有直接依赖，也不等于实现 WebDAV 同步。

本项目 `RimeProcess.RunAsync` **已经使用 `UseShellExecute=false` 和 `CreateNoWindow=true`**，不是所有调用都尚未隐藏。但当前仅接受 sync/deploy/skin-reload/diagnostics，没有 `/dict` 参数及标准输出读取接口。若接入新词典命令，还需处理输出、错误码、取消和维护失败恢复，同时核对托盘、安装器、资源管理器双击等入口。

### 建议与选择

1. **皮肤工作继续，与此项解耦。** 暂保留现有 GUI 部署器。
2. 用户需要新增词典工具时，再成组移植该提交及调用适配，并验证中文路径、参数引号、输出编码、退出码和维护状态恢复。
3. 如果用户要求部署器始终保持 GUI 子系统，可以评估独立 CLI helper 或其他输出桥接；这是替代设计，尚未实施，成本也高于直接适配上游。

该项截至本报告**尚未移植**；前轮两项稳定性修复及完整构建已经完成，详见 [上游整合记录](UPSTREAM-INTEGRATION-20261021.md)。本报告不代替用户对此选择的决定。

## 8. 背景图片 PR、许可证与实施顺序

### 背景图提案状态

官方 [PR #1939](https://github.com/rime/weasel/pull/1939) 截至查证时为 **open、merged=false**，head 为 `ecfd0244a926e32d8439326bddd82f15b4d99b6f`。它增加候选窗背景图，不是已经发布的官方角色皮肤框架，也不能直接覆盖我们所需的多图层包、外扩角色和双注音布局。

### 代码复用边界

Fcitx5 所读主题源码标注 LGPL-2.1-or-later，Trime 所读源码为 GPL-3.0-or-later，fcitx5-webview 为 GPL-3.0，Fcitx5 Android 为 LGPL-2.1，Overlay 为 MIT，Mozc 所读 Windows 源码有 BSD 三条款式许可。实际移植应逐文件保留许可证和作者；本轮没有复制产品实现代码。theme-designer 的核对树未见明确许可文件，不能默认允许直接搬运其源码；可以参考公开行为另行实现。皮肤图片本身的授权与程序许可证分别判断。

### 建议的下一轮顺序

| 顺序 | 工作 | 验收依据 |
|---|---|---|
| 1 | 统一有效偏好、框体锚点、完整画布与光标定位模型 | 同一包与同一候选数据的网页／原生几何对照；有无注音均覆盖 |
| 2 | 增加输入行定位预览、用户偏移与自动避让 | 100%／150%／200% DPI、上下边缘、长短候选、装饰命中不扩张 |
| 3 | 补资源 revision、缓存失效与窗口所有权诊断 | 隐藏再显示、应用切换、皮肤切换、资源失败保留旧状态 |
| 4 | 完善皮肤移除及同标识冲突交互 | 删除活动版本有回退；覆盖失败可恢复；旧包兼容 |
| 5 | 再决定 DComp 迁移和词典 CLI | 分别给出完整后端对照与词典操作回归，不以合并成功代替验收 |

这份顺序是调研后的建议，未启动修改。下一轮若实施，代码与自动化测试由代理完成；用户日常窗口、键鼠和输入法试用继续由用户本人执行。
