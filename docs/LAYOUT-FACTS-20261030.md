# PR 06：独立原生布局复核与字体证据边界

本轮复用 repair 既有入口，不改生产布局、鼠标契约或设计器。PR 06 仅部分完成：取得新本机原生事实，真实 Canvas／字形基线差异仍未验收。

## 实际运行

- 原生程序来自已校验的 .165 artifact，源码 `5dff81fa67ef0f76996dab610028725939579880`，x64 exe SHA256 `AD5F88992110F71EA3FEDB563AC5096953EBB074C5944B0997274438E1C86DD5`。
- 输入是当前 `tests/fixtures/skin-anchor-layout-v1.json`，SHA256 `8E7ADA6F1080A8E941E119D8229DC3A739E433A05377A1F3AF359B8518721A5D`。
- 通过 CodeGraph 与源码核对：`Preview.cpp` 的 `--layout-facts` 分支在 `CreateWindowW` 前返回，复用生产 `HorizontalLayout::DoLayout`，只创建内存 DC／DirectWrite 资源，无 HWND、会话或桌面按键。
- Windows 本机 x64 运行后，`node tools/native-layout-facts.cjs` 对原始输出重新比较，得到 `PASS ... 24 independently computed fixed layouts; 24 DirectWrite facts`。96／144／192 DPI、混合／空／隐藏／旁注、长候选均包含。仅运行 x64；Win32 引用 .165 CI 原有结果，没有冒称本次也执行。
- 原始输出、比较结果和二进制／输入摘要保存在 `.tools/pr06-layout-6f133049ce244363893b575255acbd58/`。提取六行合成数据进入[CSV](benchmarks/2026-10-30-native-layout-review.csv)，不包含用户输入。

复现命令（路径使用独立输出，程序文件须先核对摘要）：

```powershell
<.165原生预览程序> --layout-facts tests/fixtures/skin-anchor-layout-v1.json <新的原生报告.json>
node tools/native-layout-facts.cjs <原生报告.json> <新的比较报告.json>
```

## 尺寸事实

| 场景 | DPI | 原生真实字体框 | 网页固定测量框 |
| --- | ---: | ---: | ---: |
| 混合注音 | 96 | 160 × 56 | 128 × 64 |
| 混合注音 | 144 | 237 × 84 | 192 × 96 |
| 混合注音 | 192 | 317 × 111 | 256 × 128 |
| 长候选 | 96 | 280 × 56 | 204 × 64 |
| 长候选 | 144 | 418 × 84 | 306 × 96 |
| 长候选 | 192 | 558 × 111 | 408 × 128 |

这些值的测量源有意不同：网页列是夹具固定字宽／行高，**不是本机 Canvas 实测**。同源固定值的 24 组布局另行通过 ≤1 物理像素对照，不能把上表差异视为新发现的生产布局错误或误差上限。原生 `textBottom` 是文字矩形底边，不能作为字形 baseline 与 Canvas 的 actualBoundingBoxAscent 相减。

请求字体为 Microsoft YaHei UI，候选／序号 12 pt、注释 10 pt。系统文件 `msyh.ttc` SHA256 `D79C55E68B1131EEA0CC1C47BE4F572D964F28C682E143DB2AD09C1E4CB07A3F`；这里只证明该文件存在，不保证每个字形没有 fallback。尚未验证实际字体 face／版本与字形基线。

## Canvas 对照草案与阻碍

新增 `tools/compare-canvas-font-facts.cjs`，读取独立原生 JSON，在全新 headless 浏览器上下文测量相同文字，记录 requested font、DPR、Canvas 宽度／ink ascent／descent，换算物理像素并输出差异事实，不设字体等值门槛，不对未知原生 baseline 填假值。页面请求会被拒绝，不访问真实输入、网络或日常浏览器资料。

初次缺少依赖按既有锁文件还原成功；Node 语法检查通过。真正运行在 headless Edge 启动处失败，详见[独立问题单](issues/2026-10-30-headless-font-browser-startup.md)，按 AGENTS 停止该路径，浏览器内逻辑未执行，不用本机失败或别的 CI 浏览器通过代替本轮字体对照。

后续须高级模型核对浏览器环境；原生真实字形／行 baseline 若需扩展，应另作只读事实接口设计，保留候选命中与编辑器选择的不同职责。跨屏及桌面视觉仍由用户验收。本轮不为追求“逐像素一致”修改字体、留白或容差。
