# 上游稳定性修复审查（2026-10-21）

## 范围

用户要求暂停候选窗位置／皮肤排查，先处理上游冲突。拉取官方引用后，上次审查点 `4aea197` 至 `82b50c9` 新增三项提交。工作分支起点为 `17b5c22`，本轮没有整体合并此前已决定暂缓的绘制重写。

拉取前工作区没有未解决冲突，只有用户保留的 `tools/SkinFileTrace/README.md` 修改。两项稳定性修复均以 `cherry-pick -x` 干净移植，没有发生文本冲突；原作者及上游引用已保留。

| 上游提交 | 本地提交 | 作用 |
|---|---|---|
| `b15084d4ac5cff04afa33391df2d692e88f29e1e` | `e5a0d4d` | 宿主已清除 TSF 组合指针时，后续中止流程仍结束候选 UIElement，避免遗留已启动状态 |
| `82b50c9e9b9ee0bd31357db6f25ddc804546c7f7` | `a53e007` | 两字段预编辑范围不再读取不存在的第三个光标字段，保留默认 cursor=-1 |

这两项修复没有改变皮肤格式、UIStyle 序列化、词库、学习数据或安装路径。候选生命周期修复与皮肤显示有关，但还不能据此认定它是用户偶发图片消失的根因。

## 暂未移植的新增项

`2c587279e4f341a447bebefd94d6519847d2cced` 提供 `/dict list/backup/restore/export/import`，并将 WeaselDeployer 改为控制台子系统。图形对话框没有移除，但托盘、安装器和配套程序的启动方式需要兼容处理，避免控制台闪现及改变既有调用行为。已向用户说明这一取舍；本轮稳定性修复不依赖该功能，等待用户选择是否另行适配。

此前暂缓的 DirectComposition／Direct2D 重写、传统配置器重构、旧兼容组件移除及日志体系替换，维持原决定，详见 [上次移植记录](UPSTREAM-INTEGRATION-20261017.md)。因此本分支仍不是与官方 master 完全相同的提交树。

## 回归与构建

- `tests/upstream_response_test.cpp` 使用真实 ResponseParser／ContextUpdater，增加两字段范围、三字段范围及不完整范围的断言。
- `tests/upstream_abort_test.cpp` 由测试脚本从生产文件抽取实际 `_AbortComposition`，覆盖活动／宿主已终止组合，以及 clear 的两个取值。仅替换外部接口，不创建窗口、COM 会话或 Rime 资料。
- 将修复前 `17b5c22` 的实际中止函数编入同一夹具作为负控制，要求复现未结束 UIElement 的失败；不能仅以编译成功代表修复有效。
- 完整 Actions： [39623134084](https://github.com/kaiye2343/weasel/actions/runs/39623134084)，源码 `2a2f57d75195dbc8ddf038a3743c562693aea35e`，结果成功。完整 x86／x64 编译、管理器／设计器、原生皮肤／布局回归、Rime 官方学习快照测试、安装包与校验清单生成均通过工作流门槛。
- 目标测试输出 `PASS candidate abort: active and host-terminated compositions, clear/preserve`；修复前负控制输出 `FAIL abort left a registered UIElement or changed cleanup order` 并按预期返回 1，测试脚本确认正负控制成功。真实解析器的可选光标字段断言也通过。
- 完整产物 ID `12356805588`，归档大小 `135386910` 字节。本轮没有下载归档，不宣称已在本机核对安装包文件或进行日常实机验收。
- 本轮没有安装、重启日常服务或修改 Rime 资料；没有执行本机桌面操作测试。
- 用户保留的 README 修改未纳入提交，SHA256 仍为 `B60CBA578020934A683AFDD8390EFFF88A4CAB8B8A9E67169E9E3CEF072CAD13`。

## 暂停点

继续开发时回到以下反馈：候选窗位置偏移及带模拟输入的预览、装饰外扩边界避让正在输入的文字、偶发皮肤不加载。开始菜单／任务栏搜索框的候选是否由宿主接管尚未实机确认；现有渲染日志没有解码失败记录，不能直接断言为缓存问题。

此前长串输入性能对照已完成，核心查询优化尚未实施，见 [官方／改版对照](PERFORMANCE-20261018-OFFICIAL-COMPARISON.md)。
