# Windows 搜索框装饰图片缺失：访问权限诊断

查证日期：2026-10-22。已安装版本：`0.17.4.117.80e3e46`。

后续实现：用户批准独立图片缓存方案后，已完成资源发布、服务事务与容器日志采集，`0.17.4.124.4928ba9` 的完整 CI 通过，见 [发布机制与验证](SKIN-ASSET-PUBLICATION.md)。尚未安装日常环境；下文“尚未实现”保留为诊断当时的状态。

## 结论与范围

**本次用户重启后复现的故障，直接原因是搜索宿主无法读取皮肤图片。** 系统允许小狼毫显示原生候选窗，图片加载失败后的只读文件打开探测返回 `ERROR_ACCESS_DENIED (5)`；当前宿主没有成功提交任何图片资源。

这解释了截图中候选文字、配色和虚线边框存在，而装饰全部缺失。缓存和绘制后端尚未得到可用图片；重复清缓存、重启或仅替换 GDI+／D2D 不能补足文件读取权限。

这是这次 SearchHost 现场的结论，不宣称所有历史应用中的图片消失、长串输入延迟或“问题1”都属于同一原因。本轮仅只读采集与源码研究，没有修改程序、目录权限、配置和学习数据库，没有安装、重启服务或操作用户桌面。

## 1. 重启后的证据

用户截图时间约为北京时间 18:19。随后保存对应进程、日志及资源元数据。

| 检查项 | 结果 |
|---|---|
| 搜索宿主 | SearchHost，PID 18020，18:16:45 启动 |
| 已加载前端 | System32 的 `weasel.dll`，内存 PE timestamp `1792641582`，SizeOfImage `1318912`，与本轮安装产物一致 |
| 旧版排除 | 前次旧模块 timestamp `1792250298`、SizeOfImage `1204224`，本次不符 |
| 宿主令牌 | `TokenIsAppContainer=true`；完整性 SID `S-1-16-4096`，即低完整性 |
| 候选窗归属 | `native_selected native_allowed=1 window_created=1 requested_images=2 result=0` |
| 图片读取 | `requested=2 decoded=0 layer=0 stage=load gdip=2 file_error=5 dimensions=0,0`，多次重试一致 |
| 图片是否存在 | 当前棉花娃娃变体的两个文件均存在，普通用户可读，已保存大小和 SHA256 |
| 文件 DACL | 两个文件仅有 SYSTEM、Administrators、当前用户的完全控制 ACE，没有应用容器的读取授权 |

`gdip=2` 本身是 GDI+ 的笼统失败状态；`file_error=5` 来自失败后独立执行的 `CreateFileW(GENERIC_READ)` 探测，不冒充 GDI+ 内部原始 Win32 错误。本次两者结合宿主令牌、文件存在性和 DACL，指向可直接操作的资源访问缺口。

首层失败后生产代码停止本次加载并保留此前成功的整组图片。该宿主此前没有成功图片，因此 `decoded=0`；并非日志已经逐个证明第二层也调用解码失败。

### 日志为什么之前没有找到

`SkinDataRoot()` 使用当前进程的 `LOCALAPPDATA`。普通桌面进程与 SearchHost 的值不同。SearchHost 的日志实际位于其包数据的：

```text
%LOCALAPPDATA%（SearchHost 进程视图）\ShorekeeperWeasel\render-logs
```

在本机外部观察，对应 `Packages\<搜索宿主包>\AC\ShorekeeperWeasel\render-logs`，并不在普通桌面用户的外观日志目录。只检查普通目录时，无法看到搜索宿主的失败；不能据“该目录无日志”推断系统接管了候选窗。

私人现场保存在忽略目录 `.tools/search-skin-reboot-20261022-182333/`。其中 `searchhost-appcontainer` 保存相同 PID 的两类日志，`searchhost-token-pe.json` 保存令牌和 PE 元数据，`requested-assets-metadata.json` 保存文件元数据，`evidence-sha256.json` 保存日志摘要。用户数据和包目录不写死进产品。

## 2. 本项目的实际失败链

```text
普通用户服务读取活动皮肤并验证 PNG
  → UIStyle 传递样式数值和图片绝对路径
  → SearchHost 内的 TSF 前端收到样式
  → 数值样式直接用于绘制文字、背景和虚线框
  → _UpdateSkinGeometry 调用 LoadSkinBitmap(path)
  → SearchHost 的容器权限不能读取私人皮肤 PNG
  → 加载失败，decoded=0，保留文字候选框
```

CodeGraph 核对 `_UpdateSkinGeometry → LoadSkinBitmap`，再检查实际代码：

- `WeaselUI/SkinPackage.cpp`：`Asset` 在服务侧解析并验证资源，最终使用物理路径。服务可读不意味着每个宿主都可读。
- `WeaselUI/SkinAssetPath.h`：`ResolveSkinAssetPath` 修正路径视图；它不授予访问权限。
- `WeaselUI/SkinImageLoader.h`：前端依然调用 `Bitmap::FromFile`，失败后只读探测文件。
- `WeaselUI/WeaselPanel.cpp`：图片整组成功后才替换缓存；首次失败的宿主没有旧资源可以回退。
- `WeaselTSF/CandidateList.cpp`：`BeginUIElement` 后本次选择原生候选窗，排除本次为系统代绘造成的装饰缺失。

微软明确说明：TSF 加载到应用内的输入法 DLL 受相同 AppContainer 限制；需要在容器内访问的文件必须放在可读位置，或对具体资源配置适当 ACL。[官方 IME 要求](https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements#ime-must-work-in-app-container)

## 3. 其他开源 Windows 输入法的参考

只读核对固定提交，没有安装或执行这些输入法。

| 项目与版本 | 实际实现 | 可借鉴部分与限制 |
|---|---|---|
| Mozc `f183d4d3a7ba139d7cb2252c05f608e9f8266aef` | 候选 GUI 在独立 renderer 进程接收 IPC；Windows 候选窗通过 `LoadImage` 从自身模块读取内置位图，并按 DPI 更新资源 | 资源生命周期由绘制方掌握，避免让每个输入宿主独立打开任意私人路径。但其内置 logo 不等同于我们的可导入角色皮肤；照搬独立进程仍需处理协议、窗口和故障恢复 |
| PIME `e0ddf2ffc180837c2e9f8be363de4455c7532962`，引用 libIME2 `78e61045956960a511f85eb823e0fd65fa8c94b9` | 在 TSF 中创建候选窗，经 `BeginUIElement` 决定是否显示；libIME2 用 GDI 画背景、选中项和文字 | 支持根据宿主环境调整边距，遵守 UI 所有权。核对候选绘制代码没有可导入任意 PNG 的对应机制，不能从其文字候选正常推断图片文件可读 |

源码依据：

- [Mozc renderer 说明](https://github.com/google/mozc/blob/f183d4d3a7ba139d7cb2252c05f608e9f8266aef/src/renderer/README.md)、[Windows renderer 服务](https://github.com/google/mozc/blob/f183d4d3a7ba139d7cb2252c05f608e9f8266aef/src/renderer/win32/win32_server.cc)、[候选窗的 LoadBitmapFromResource／UpdateDpiDependentResources](https://github.com/google/mozc/blob/f183d4d3a7ba139d7cb2252c05f608e9f8266aef/src/renderer/win32/candidate_window.cc)。
- [PIME createCandidateWindow](https://github.com/EasyIME/PIME/blob/e0ddf2ffc180837c2e9f8be363de4455c7532962/PIMETextService/PIMETextService.cpp)、[实际引用的 libIME2 CandidateWindow](https://github.com/EasyIME/libIME2/blob/78e61045956960a511f85eb823e0fd65fa8c94b9/src/CandidateWindow.cpp)。

Linux Fcitx5 的九宫格和缓存、Android Trime 的注音排版仍可参考，但不能作为 Windows AppContainer 文件访问可用性的验证。外部进程图片外挂也不代表与原生文字框合成、定位和透明生命周期完全一致。

## 4. 建议的修复边界（尚未实现）

优先采用**独立发布的皮肤图片资源缓存**，继续保留当前统一原生画布：

1. 原皮肤包、选择状态、个人配置与词库保持私有；普通用户管理端验证皮肤后，只发布被允许的 PNG／必要图片资源。
2. 发布目录与私人资料目录分离，对发布资源提供容器所需的最小只读权限；不向整个 Rime 目录授权，不要求用户将服务改为管理员运行。
3. 使用内容摘要和不可变版本目录；完整写入、验证后再切换引用。不得用指回私人源文件的链接代替真正发布。
4. 前端只在资源版本变化时读取并解码，后续使用现有图片／缩放／固定合成缓存。实际候选变化不重复发布图片。
5. 将九宫格和普通背景图片也纳入同一路径，避免只修角色、遗漏框体图片。
6. 对失败结果保留旧有效资源与诊断信息；汇总日志按进程包信息发现实际容器目录，不写死 SearchHost 包名。

这比立即迁移 Mozc 式整套绘制进程的范围小。另一条路线是经受控资源通道传递图片或解码像素，也需要审核 IPC 容量、来源验证、版本和缓存失效，不能把大图片放进逐键候选消息。

**缓存发布目录、ACL、跨用户隔离与删除策略还需实现并测试，不能把本节建议写成已修复。** 本轮没有扩大任何本机目录权限。

### 修复后的验收门槛

- 隔离 AppContainer 下：私人源文件读取被拒绝；已发布图片可只读，不可写；私人词库仍不可读。
- 同一容器令牌下真实 PNG 解码及像素／Alpha 核对，不只验证普通进程测试通过。
- 导入、覆盖、删除、资源发布失败和皮肤切换有原子回退；跨用户资源不串用。
- 换页和长串输入不会反复读取或发布固定图片；版本变化才重建对应缓存。
- 用户本人在新版 SearchHost 与普通应用验证，按各自 PID 对齐 presentation／render 日志；期望 native 窗口且全部目标图片成功加载。
- 仍保留系统明确选择 host UI 的降级边界，不通过注入或挂钩强制抢占系统候选窗。
