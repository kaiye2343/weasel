# 部署器词典命令行适配（2026-10-22）

## 来源与范围

移植官方 [`2c587279e4f341a447bebefd94d6519847d2cced`](https://github.com/rime/weasel/commit/2c587279e4f341a447bebefd94d6519847d2cced)，保留作者与 cherry-pick 来源；本地基础提交 `ba38bad`，适配提交 `b8c8cab`、`170d9e5`。

WeaselDeployer 改为 Console 子系统，MSBuild 与 xmake 同步调整。只解决本轮入口冲突，没有顺带引入上游传统配置器或渲染后端重写依赖。

## 命令与兼容

```powershell
# 在安装目录的终端执行；以下是用法说明，本轮未在日常资料上运行。
.\WeaselDeployer.exe /dict list
.\WeaselDeployer.exe /dict backup work
.\WeaselDeployer.exe /dict restore 'D:\个人备份\work.userdb.txt'
.\WeaselDeployer.exe /dict export work 'D:\个人备份\work.tsv'
.\WeaselDeployer.exe /dict import work 'D:\个人词库\work.tsv'
.\WeaselDeployer.exe /dict help
```

`work` 只是示例词典名，实际以 list 输出为准。backup 输出 Rime 本设备同步目录中的快照路径；export 是文本词条导出，不能代替保留学习信息的用户词典快照。restore/import 通过官方 levers 机制处理，不直接覆盖活动 `.userdb`。

退出码：0 成功；1 操作、资料文件、API、互斥或服务恢复失败；2 命令参数错误。标准输出／错误输出使用 UTF-8，保留中文词典名和含空格的路径。自动化调用者应分别读取两个流并检查退出码，不以标准输出非空判断成功。

未带子命令的 `/dict` 仍打开传统词典管理对话框；空参数仍走传统方案配置，`/deploy`、`/sync`、`/install` 原有入口保留。托盘“输入法设定”继续打开现代设置中心。GUI 路径脱离控制台，服务与安装器隐藏启动部署器；设置中心原有 `CreateNoWindow=true`、`UseShellExecute=false` 保留。

本轮没有在设置中心新增词典操作按钮，已有同步和部署入口继续使用原调用。新的词典 CLI 先作为可调用工具提供；后续界面接入仍须遵守备份、变更预览与保存／部署分离规则。

## 本地适配

- 检查 levers 模块、所需函数及 installation_update；初始化失败不继续词典操作。
- 成功初始化的枚举器在异常时释放；关闭互斥后恢复服务维护状态，失败返回非零。
- 只在本次尝试进入维护模式后恢复，避免离线操作结束时改变后来启动的服务。
- restore/import 在维护前检查输入文件，拒绝缺失文件和目录。真实固定版本 librime 的 import 在缺失输入时可能返回 0，因此不能仅把负数当作全部文件失败。
- backup 不再使用 MAX_PATH 大小的 UTF-8 缓冲，且空同步目录不返回貌似有效的相对快照路径。

## 验证方式与边界

`tools/test-deployer-cli.ps1` 从生产源提取 CLI 与分发函数编译，使用固定 SDK 的真实 ABI。

1. 模拟互斥、IPC 与 API，覆盖五种命令、旧图形入口路由、参数数目、中文参数、接口缺失、初始化失败、枚举异常、恢复重连失败与资源清理顺序。
2. 使用实际 librime DLL、全新临时资料目录进行 import/export/backup/restore/list 和失败文件测试；IPC 与命名互斥仍为替身，不能连接本机日常服务。
3. 编译适配前的真实 CLI 为负控制，要求检测到被忽略的初始化失败。
4. 从生产入口抽取流设置，实际经过 CRT 参数解析和管道，验证 UTF-8 stdout/stderr 与含空格中文参数。
5. 成套编译后检查 x86/x64 部署器 PE 子系统；完整管理器、皮肤、注音、学习快照及安装包继续走既有工作流。

本机缺少 C++ 编译器和 .NET SDK，完整编译与上述 Windows 回归在 GitHub Actions 执行。本轮未安装、未修改用户资料、未调用日常词典工具；桌面可见性、UAC 与实际应用试用继续由用户完成。构建结果在本轮整合记录中另记，不能仅以此文档作为通过证明。

### 已完成的构建

- [Actions 39635674084](https://github.com/kaiye2343/weasel/actions/runs/39635674084)，源码 `170d9e54fdfc81d3b4584cd710f97395d91a4469`，完整成功。
- 新增生产 CLI、适配前负控制、真实 librime 词典操作、UTF-8 双流和 CRT 参数边界测试均通过；x86/x64 成品 PE 子系统检查通过。
- 既有 56 项设计器测试、24 项隔离浏览器测试、110 项管理器测试，以及原生皮肤／注音／缓存、官方学习快照和完整安装包门槛通过。
- 产物 `12362340721`，归档大小 `135426915` 字节。GitHub 返回归档摘要 `sha256:b6fe61dd91d5b876f4c1f615aeb3f634c6a56a19a1f5e11be62f3af0eb9d475d`；本轮未下载此完整安装包，未声称已在本机逐文件核对或安装。
- 首轮真实回归 `39634527063` 检测到缺失文件被当作零条导入成功，修正后重新跑完整工作流；没有跳过该断言。

## 渲染任务分离

`shorekeeper` 保持原默认绘制后端。独立分支 `codex/dcomp-skin-migration` 先建立 DComp 呈现现有完整皮肤画布的实验基线与 GDI 回退，不宣称已完成原生 D2D 绘制迁移或已获得性能收益。
