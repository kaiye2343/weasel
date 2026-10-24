# 网页与原生共享布局夹具

## repair 后续批次（2026-10-24）

原生事实输出与点击／松开／翻页夹具已按用户授权的 repair 指导实施，[完整 CI 40106592815](https://github.com/kaiye2343/weasel/actions/runs/40106592815) 已通过两架构全部固定测量和事件检查。它直接调用生产 `HorizontalLayout::DoLayout`，与下方网页供给框体的 D1a 不同。详见 [原生事实及事件范围](REPAIR-EXECUTION-20261024.md)。

- `tools/test-native-layout-facts.ps1`：使用已构建的两个架构预览程序；`--layout-facts` 在创建窗口前输出固定测量和真实字体报告，不连接输入法。
- `output/archives/native-text-facts-*.json`：原始合成输入、原生文字／注音／高亮／命中矩形及 DPI／坐标转换。
- `output/archives/native-text-comparison-*.json`：24 组固定测量对照与 24 组真实字体事实。网页与固定测量允许 1 物理像素取整差；字体实测不作等值保证。
- `upstream_hover_test.cpp`：提取的真实悬停／按下／松开事件，在受控矩形上验证回调和翻页优先级。其矩形仍是模拟值，与前一项真实生产布局报告分层验收。

## D1a：图片锚点与画布

这是 D1 的第一批，**仅验证装饰图片的锚点、完整画布与原点换算**。不更改产品布局、不修复已交接的 C1b 崩溃，也不将 D1 整体标记完成。

共享输入：`tests/fixtures/skin-anchor-layout-v1.json`，`schemaVersion=1`、`scope=decoration-anchors`。只包含合成候选、固定测量参数、注音模式、图片矩形和模拟框体偏移，没有真实词库、输入或图片文件。

八个场景各在 96／144／192 DPI 执行，共 24 组：部分缺注音、全空注音收起、全空注音保留、隐藏注音、旁注、四角锚点、无装饰、长内容。框高变化用于检查底部锚点，不表示测量了原生文字行高。

## 调用链与证据边界

1. `tools/shared-skin-anchor-fixture.cjs` 读取版本化数据，调用真实网页 `SkinGeometry.layout`，使用固定文本测量值取得网页框体与图片矩形。
2. 同一脚本生成临时 C++ 输入。框体尺寸来自上述网页结果；坐标按当前原生 `DPI_SCALE` 的整数截断规则转换为物理像素。框体偏移模拟透明边距对可见框体原点的影响。
3. `tests/shared_skin_anchor_test.cpp` 直接调用生产 `WeaselUI/SkinGeometry.h` 的 `PlaceSkinImage` 和 `ExpandSkinCanvas`，输出 JSON；没有复制锚点算法，不启动窗口或读取皮肤／Rime 文件。
4. Node 比较程序检查输入 SHA256、场景顺序、DPI、框体、各层原坐标、扩展画布、原点及平移后的窗口坐标。每个坐标最多允许 1 个物理像素的截断差。

**原生程序接受的是已经给定的框体尺寸。** 因此这批证明的是同一框体下图片锚点语义一致，不是独立验证原生候选测量、文字排版、真实字体或网页与原生整体尺寸一致。没有调用原生 `HorizontalLayout::DoLayout`，也没有验证图片解码、Alpha、绘制稳定性、DComp、屏幕位置或鼠标命中。

注音收起／保留等场景在此仅由网页产生不同框高，供生产锚点函数消费。原生注音行高、候选基线和部分缺注音的实际排版仍待 D1 后续批次。

## 运行方式

本地无窗口的数据与比较器基本检查：

```powershell
node tests/shared_skin_anchor_fixture_test.cjs
```

已配置 MSVC 的隔离环境：

```powershell
./tools/test-shared-skin-anchors.ps1 -Architecture x64
# 切换为 x86 编译环境后：
./tools/test-shared-skin-anchors.ps1 -Architecture Win32
```

脚本在 `.tools/shared-skin-anchors-<随机标识>` 生成 include、exe 和原始 JSON。使用 `/O2 /W4 /WX /MT`，通过生成输入内的 `static_assert` 核对编译位宽；测试进程超时后仅终止自身创建的进程。无日常窗口、输入法、注册表或用户资料操作。

两架构的结果分别归档为 `output/archives/shared-skin-anchors-x64.json` 和 `shared-skin-anchors-Win32.json`。输出字段：

- 顶层：`schemaVersion`、`scope`、`coordinateSpace=physical-pixels`、`fixtureSha256`、`cases`。
- 场景：`id`、`dpi`、`frame`、`canvas`、`origin`、`layers`。
- 图层：`id`、`rect`（原内容坐标）、`windowRect`（加入原点平移后的窗口坐标）。

不将场景来源的路径或候选正文写入原生结果；输入摘要用于防止串用旧报告。空装饰数组合法。

## 分工与状态（2026-10-24）

- DSH：单次会话 `session-47a3e84d-bdf7-4dff-9e69-aa407ce93002`，`workbuddy-global/deepseek-v4.1-flash`，`workspace-write / ask`，仅新增原生测试源文件；未运行 Shell、测试或提交。
- Codex：共享输入、Node 桥接与检查、隔离构建脚本、CI 接入、实际源码审查；原生测试不以 DSH 的 DONE 自述验收。
- 本地两项 Node 检查与 PowerShell 语法检查通过；[CI 40082835762](https://github.com/kaiye2343/weasel/actions/runs/40082835762) 中 x64／Win32 各 24 组真实编译／跨端比较通过，`/W4 /WX` 门槛保留。实际原始报告进入工作流失败附件；本机未运行 MSVC。
- 完整 CI 仍因已交接的 C1b 访问违例失败。D1a 步骤在它之前成功，不能把步骤通过等同于完整构建通过，也没有生成本批可安装的新完整包。
- 准备命中夹具时发现网页／原生命中契约差异，已另写[问题单](issues/2026-10-24-preview-hit-contract-difference.md)，停止相关修复尝试，未将命中比较记为通过。
- 发现新问题时写 `docs/issues/` 问题单并停止相关修复尝试，由用户调用高级模型处理。

## D1a 交付时未包含的后续部分（更新见顶部 repair 批次）

- 原生文字布局读取同一份候选、注释、样式和固定测量输入。
- 输出候选／序号／注音矩形、基线、分隔线和命中区域。
- 使用真实原生布局而非另写算法，分别检查固定测量与真实字体差异。
- 浏览器消费原生事实输出，并保持偏好不污染导出包。

后续接入不得把本批的网页供给框体当作“原生文字测量结果”。产品运行不依赖这些测试生成文件，也不依赖开发工具私有缓存。
