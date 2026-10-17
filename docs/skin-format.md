# rimeskin 1.0 / 1.1 格式

`.rimeskin` 是 ZIP 文件，根目录必须直接包含 `manifest.json`。仅接受 JSON 与静态 PNG，不执行脚本。格式约束见同目录 `rimeskin.schema.json`；尺寸、路径和累计资源预算由导入器进一步检查。

## 清单

必填：`schemaVersion: 1`、`id`、`name`、`author`、`version`、`minEngineVersion: "1.0"`、`preview`、`variants`。标识为最多 64 位小写英文、数字、点、下划线或短横线，不能包含连续两个点；版本为 `主.次.补丁` 数字。预览图为相对路径 PNG。

新格式 **1.1** 使用 `schemaVersion: 2`、`minEngineVersion: "1.1"`，并要求 `annotationSupport: "none" | "both"`。其余字段沿用 1.0。`none` 只支持无候选注音：即使全局偏好为上方／旁边或保持高度，也隐藏注音与分隔线；`both` 支持两种高度，继续接受全局显示偏好。1.0 保持兼容，但不能夹带此声明；旧引擎拒绝新格式，避免静默忽略能力。

两种状态共用自适应框体与素材：九宫格根据实际高度拉伸，四角图片锚点跟随实际框高重新计算。当前没有两套独立的固定布局模板。仅带注音的皮肤不在本次接口中，设计器不输出此能力。部分候选缺注音留空，同一行候选保持基线；整页无注音默认收起整行（全局保持高度选项仍对 both 生效）。

每个变体有独立 `id`、`name`、`style`、`layers`。同一包最多 32 个变体；每个活动变体最多 8 个图片层。同标识的新版本并存，导入时提示确认；同标识同版本须明确确认后替换。导入先在暂存目录验证，成功后整体移动；失败不改变当前选择。替换正在启用的版本时，保留仍存在的变体，已移除变体回退到首个变体，并清除旧的样式覆盖，以免遮蔽新设计。

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

锚点支持 `top_left`、`top_right`、`bottom_left`、`bottom_right`，相对带留白的可见候选框四角，不包含窗口为阴影预留的外部画布。x/y 为图片容器左上角的偏移，范围 ±512；width/height 为保持比例容纳图片的容器，范围 1—512。z 为 -100—100，负数在候选文字下方，非负数在文字上方，同 z 按声明顺序绘制。正 z 图片可能遮挡文字，作者应使用留白。窗口自动扩展至包含全部图片。修正前的实验原生后端误用了阴影画布角点；本轮统一为与设计器相同的可见框体角点，无需修改旧包清单。

压缩包最大 50 MiB、最多 256 个条目，解压最大 100 MiB；单图不超过 2048×2048。活动层与背景的累计解码预算 64 MiB。PNG 校验 CRC；最终由原生解码器再次检查。

## 存储与刷新

安装目录 `%LOCALAPPDATA%\ShorekeeperWeasel\skins\<id>\<version>`；选择与参数覆盖存放于同目录上级 `state.json`。独立于 Rime 配置与词库。

设置中心支持按钮或拖入单个包导入。移除仅针对所选版本；移除当前版本恢复原生外观，其他版本不受影响。覆盖和移除的可恢复副本在资料根目录的 `history/<时间-唯一标识>/package`，原选择状态在 `state.before.json`。可将 package 内的内容压缩为根目录含 manifest.json 的 ZIP，再以 `.rimeskin` 导入恢复。刷新失败会恢复原包与原选择；不部署输入方案、不触碰学习数据库。

管理器关闭后皮肤由小狼毫原生绘制。`WeaselServer.exe /skin-reload` 刷新外观，不部署、不重启、不修改方案。退出码 0 成功、2 服务未运行、3 拒绝或服务不支持该命令。加载失败保留当前样式；面板恢复原选择。

第一阶段只保证小狼毫原生横排候选窗。系统应用自己绘制的候选列表、竖排与全屏不保证装饰。动画不在当前格式范围内。独立网页设计器见 `designer/index.html`；完整分发包为 `rime-skin-studio.zip`，设置中心皮肤页也提供入口。网页不触碰日常资料，导出后由设置中心验证和导入。

## 原生预览

`WeaselSkinPreview.exe --manifest <完整路径> --variant plush`。与输入法共用包读取、布局、绘制代码；不初始化 Rime，不读取学习数据库。候选自动切换长度；1 上方注音、2 旁边、3 隐藏；可拖动测试屏幕边缘；Esc 关闭。


## 网页设计与原生预览

网页中的文字度量、阴影及高亮是近似值，最终以独立原生预览为准；图片锚点与原生使用相同的可见框体约定。预览可选择“皮肤原始参数”或“实际显示偏好”（默认 12／10 pt、紧凑密度）；后者可导入设置中心导出的个人显示偏好 JSON。预览覆盖不写入皮肤包。全局用户字号、密度和注音位置仍可覆盖皮肤默认样式（none 的能力约束优先）。新建为 1.1；导入 1.0 后保持版本，修改能力声明时升级。

注音能力由服务端解析并落实到最终 `annotation_position`；能力标记只用于本地清单与原生预览，不额外加入 IPC 样式档案。本轮字段顺序与实际已安装的 0.17.4.64 保持一致。0.17.4.74—0.17.4.87 中间实验包曾包含额外字段，不能将其单个组件与本版混用。安装仍成套更新 x86／x64 前端、服务及部署器；不要只替换某一个 DLL。旧配置与学习资料无需更改。
