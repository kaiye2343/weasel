# 守岸人小狼毫改版

项目目录：`E:\code\输入法`。官方基础版本：Weasel 0.17.4（`9cc96e2`）。

远端：<https://github.com/kaiye2343/weasel>；官方上游：<https://github.com/rime/weasel>。
工作分支：`shorekeeper`。

## 本版实现

- 移植上游未合并PR [#1939](https://github.com/rime/weasel/pull/1939)的背景图支持，保留原作者的两次提交。
- 新增 `style/skin/images`：最多8个静态PNG等GDI+支持的图片，以候选内容四角为锚点。
- 自动扩大窗口容纳图片；候选内容坐标、鼠标命中和截图坐标随之转换。
- 候选内容和装饰在同一个预乘Alpha缓冲区合成，再一次提交给分层窗口。
- 图片按比例缩放；根据当前显示器DPI缩放位置和尺寸；图片按配置缓存，销毁窗口时释放。
- 装饰启用时，在候选框内部增加虚线边框。
- 三套样例：抱男性漂泊者棉花娃娃、吃瓜、星蝶Q版。

装饰只在小狼毫自身绘制的横排候选窗且存在候选时启用。竖排、全屏和系统应用自行绘制的候选列表不应用装饰。GIF只显示静态帧，不含动画。图片文件修改后需要重建窗口或重启服务以刷新缓存。

## 构建与下载

仓库的 **Actions → Shorekeeper build → Run workflow**，选择 `shorekeeper`。推送到该分支也会构建。

工作流使用Windows 2022、VS2022/v143、Boost 1.84.0、librime 1.13.1；构建x64及x86通信两端并生成NSIS安装包。产物 `shorekeeper-weasel-<commit>` 保存14天，包含安装包、三套皮肤配置、素材、提交编号和SHA256校验值。

构建成功只代表编译和几何测试通过，不能代替透明效果、鼠标命中、跨屏及应用兼容性的实机测试。首次测试前应先备份用户数据，明确批准安装实验构建。

本版修改了UIStyle序列化，必须安装同一次构建的完整组件。不能将本版WeaselServer或单个DLL与官方0.17.4的其他组件混用。当前安装包沿用官方安装目录和注册方式，不支持与官方版本并行注册。

## 皮肤配置

`skins/shorekeeper/plush.custom.yaml` 等是完整的示例补丁，**不要直接覆盖现有weasel.custom.yaml**。后续安装测试时，应先备份，保留现有用户补丁，再合并示例中的patch项；将 `skins/shorekeeper` 素材目录复制到Rime用户目录。配置改动后用官方部署器部署。

单个装饰示例：

```yaml
patch:
  style/skin/images:
    - path: skins/shorekeeper/shorekeeper-right.png
      anchor: bottom_right
      x: -22
      y: -149
      width: 221
      height: 184
```

锚点支持 `top_left`、`top_right`、`bottom_left`、`bottom_right`。x/y指图片容器左上角相对锚点的偏移，单位是96DPI下的逻辑像素；width/height为按比例容纳图片的容器大小。负数使图片跨过候选框边缘。路径相对Rime用户目录解析，可含空格与中文。

每层width/height须在1至512之间；偏移绝对值不大于512。源图长宽最多2048。无效或缺失图片跳过，不改变输入引擎。

切换角色：将 `plush`／`watermelon`／`butterfly` 对应样例的 `style/skin/images` 合并到用户补丁并部署。第一阶段还没有托盘一键切换；无需重部署的角色切换列为后续功能。

## 问题1与回滚

“问题1”是实际启用的方案退回内置全拼、繁体竖排或不能正常选词。此前故障已在官方未改版程序上出现，本改版不宣称解决其根因。故障时先保存日志、配置、进程、当前方案，再重启恢复。

当前开发未写入本机Rime用户目录，也未安装实验构建。将来若测试后回滚，应退出小狼毫，使用官方0.17.4完整安装包恢复程序；用户学习数据库原样保留。恢复用户配置时使用修改前备份，避免在服务运行时覆盖userdb。

## 维护

```powershell
git status
git fetch upstream --tags
git log --oneline -8
```

保持改版提交小而可审查。上游PR合并后再核对移植代码；不要盲目合并master。代码沿用上游GPLv3，保留LICENSE.txt与原作者归属。角色素材独立于上游代码许可。
