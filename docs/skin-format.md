# rimeskin 1.0 格式

`.rimeskin` 是 ZIP 文件，根目录必须直接包含 `manifest.json`。仅接受 JSON 与静态 PNG，不执行脚本。格式约束见同目录 `rimeskin.schema.json`；尺寸、路径和累计资源预算由导入器进一步检查。

## 清单

必填：`schemaVersion: 1`、`id`、`name`、`author`、`version`、`minEngineVersion: "1.0"`、`preview`、`variants`。标识为最多 64 位小写英文、数字、点、下划线或短横线，不能包含连续两个点；版本为 `主.次.补丁` 数字。预览图为相对路径 PNG。

每个变体有独立 `id`、`name`、`style`、`layers`。同一包最多 32 个变体；每个活动变体最多 8 个图片层。同标识的新版本并存，同版本不覆盖。导入先在暂存目录验证，成功后整体移动；失败不改变当前选择。

## 样式

`fontFace` 为已安装字体名称；不安装皮肤内字体。`fontPoint` 范围 8—48，`commentFontPoint` 为 8—32。

`annotationPosition`：`above` / `beside` / `hidden`。显示 Rime 已提供的整条候选注释，不逐字推断发音。无注释时不补内容。

可选 `annotationSeparator: {"enabled":true,"color":"#55000000","thickness":1,"fadeEnds":true}`，控制上方注音和候选文字之间的静态分隔线。粗细 1—4 DIP；默认半透明黑色、两端渐隐。仅在注音行存在时绘制，范围限于文字行，不延伸到装饰区。旧包无需增加此项。

`paddingLeft/Right/Top/Bottom` 和 `candidateSpacing` 范围 0—256；单位为 96 DPI 逻辑像素。内容区会计入这些留白；图片可以使用留白和框外空间。作者应为角色预留足够空间，框架不会判断图片中哪些像素属于角色。

`cornerRadius` 范围 0—64；`shadowRadius` 为 0—32；`borderWidth` 为 0—12；`borderInset` 为 0—32。`borderStyle`：`none` / `solid` / `dash`。颜色使用 `#AARRGGBB`：`backgroundColor`、`textColor`、`commentColor`、`highlightColor`、`highlightTextColor`、`borderColor`、`shadowColor`。

可选 `frame: {"path":"frame.png","slice":[left,top,right,bottom]}`，使用九宫格 PNG 拉伸背景。左右、上下切片总和须分别小于图片宽、高，保留非空中心。`frame: null` 使用纯配色。素材以相对包根目录路径解析，禁止绝对路径、反斜线、冒号、空路径段、`.`、`..`、Windows 保留设备名及链接。

## 图片层

```json
{"path":"character.png","anchor":"bottom_right","x":-176,"y":-149,"width":221,"height":184,"z":1}
```

锚点支持 `top_left`、`top_right`、`bottom_left`、`bottom_right`，相对带留白的候选内容框。x/y 为图片容器左上角的偏移，范围 ±512；width/height 为保持比例容纳图片的容器，范围 1—512。z 为 -100—100，负数在候选文字下方，非负数在文字上方，同 z 按声明顺序绘制。正 z 图片可能遮挡文字，作者应使用留白。窗口自动扩展至包含全部图片。

压缩包最大 50 MiB、最多 256 个条目，解压最大 100 MiB；单图不超过 2048×2048。活动层与背景的累计解码预算 64 MiB。PNG 校验 CRC；最终由原生解码器再次检查。

## 存储与刷新

安装目录 `%LOCALAPPDATA%\ShorekeeperWeasel\skins\<id>\<version>`；选择与参数覆盖存放于同目录上级 `state.json`。独立于 Rime 配置与词库。

管理器关闭后皮肤由小狼毫原生绘制。`WeaselServer.exe /skin-reload` 刷新外观，不部署、不重启、不修改方案。退出码 0 成功、2 服务未运行、3 拒绝或服务不支持该命令。加载失败保留当前样式；面板恢复原选择。

第一阶段只保证小狼毫原生横排候选窗。系统应用自己绘制的候选列表、竖排与全屏不保证装饰。动画和完整拖拽设计器不在 1.0 范围内。

## 原生预览

`WeaselSkinPreview.exe --manifest <完整路径> --variant plush`。与输入法共用包读取、布局、绘制代码；不初始化 Rime，不读取学习数据库。候选自动切换长度；1 上方注音、2 旁边、3 隐藏；可拖动测试屏幕边缘；Esc 关闭。
