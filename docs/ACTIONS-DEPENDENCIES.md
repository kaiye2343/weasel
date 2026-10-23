# Fork 构建依赖

2026-10-24 核对官方 release、Git 标签实际 commit 和该 commit 的 `action.yml`，固定完整 SHA。仅调整 fork 使用的完整构建、文件诊断和手动性能对比；官方专用发布及 Appcast job 仍由仓库条件跳过。

| Action | 核对标签 | 固定提交 | 声明的运行时 |
|---|---|---|---|
| actions/checkout | v7.0.1 | `3d3c42e5aac5ba805825da76410c181273ba90b1` | node24 |
| actions/cache（含 restore） | v6.1.0 | `55cc8345863c7cc4c66a329aec7e433d2d1c52a9` | node24 |
| actions/setup-node | v7.1.0 | `949feb2413d6458794dcd2491c4babbbce0c15c1` | node24 |
| actions/setup-dotnet | v6.0.0 | `a98b56852c35b8e3190ac28c8c2271da59106c68` | node24 |
| actions/upload-artifact | v7.0.2 | `cf430e030ddbb5b0abf93d22962f4752f3646cd9` | node24 |
| microsoft/setup-msbuild | v3 | `30375c66a4eea26614e0d39710365f22f8b0af57` | node24 |
| ilammy/msvc-dev-cmd | v1（核对当日标签） | `0b201ec74fa43914dc39ae48a89fd1d8cb592756` | node20 |

`msvc-dev-cmd` 当前核对的官方版本仍声明 Node 20，不能将它描述为原生 Node 24 支持。保留实际 runner 的弃用提醒，不设置隐藏警告变量、不以固定 SHA 宣称弃用通知消失；该依赖的运行兼容性由完整 CI 验证。其余列出的 Action 已声明 Node 24；托管 Windows runner 满足条件仍需实际 run 验收。

缓存继续使用完整构建的架构键；性能对比仅 restore，不保存不完整缓存。Node 24、.NET 10、自包含设置中心、artifact 文件路径、隐藏失败诊断与保存期限沿用原要求。版本治理不减少原生、容器、网页或安装包门槛，也不触发 release。

后续升级须重新核对官方标签和完整提交，不能将此表作为永久最新版声明。更换 Node 运行时只解决 Action 依赖问题，不代表 C++ 警告或 PDB 损坏已解决。

官方入口：[checkout](https://github.com/actions/checkout/releases)、[cache](https://github.com/actions/cache/releases)、[setup-node](https://github.com/actions/setup-node/releases)、[setup-dotnet](https://github.com/actions/setup-dotnet/releases)、[upload-artifact](https://github.com/actions/upload-artifact/releases)、[setup-msbuild](https://github.com/microsoft/setup-msbuild/releases)、[msvc-dev-cmd](https://github.com/ilammy/msvc-dev-cmd)。
