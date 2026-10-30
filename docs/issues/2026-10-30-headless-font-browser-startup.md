# Headless 字体对照浏览器启动失败

状态：交高级模型核对运行环境；停止本路径重试，不更换浏览器、提权或修改安全策略绕过。PR 06 未取得本轮真实 Canvas 字体数据。

## 证据与范围

2026-10-30，在受限开发 shell 使用 Node v24.22.1、已有 `designer/package-lock.json` 锁定的 Playwright；首次缺少 node_modules，按锁文件执行 `npm ci --prefix designer --ignore-scripts` 成功（2 个包，无依赖变更）。随后只运行一次：

```powershell
$env:DESIGNER_BROWSER_CHANNEL='msedge'
node tools/compare-canvas-font-facts.cjs <独立原生事实.json> <新的字体报告.json>
```

Playwright 启动独立 headless Edge，无用户资料配置，未连接日常浏览器。浏览器 PID 1996 在启动后退出；创建 page、Canvas 测量或写结果之前失败：

```text
browserType.launch: Target page, context or browser has been closed
registration_protocol_win.cc:136 TransactNamedPipe: 管道已结束。(0x6D)
taskkill stderr: ERROR: The user name or password is incorrect.
process did exit: exitCode=0
```

启动参数中的临时 profile 位于沙箱 package 的 AC/Temp；这是一项环境事实，不能仅据路径认定错误原因。退出码 0 也不表示测试完成。没有生成 `font-comparison.json`，没有字体、基线或 Canvas 对齐结论。

本机无窗口原生 `--layout-facts` 已独立通过 24 固定布局／24 DirectWrite 事实，与此启动失败分开记录；没有修改产品渲染或操作用户窗口。新比较脚本仅完成 Node 语法检查，其浏览器内逻辑未执行验收。

接手时核对版本、浏览器进程日志和允许的执行方式，区分启动环境与脚本断言；保持临时独立 profile、headless 和禁止网络，不附加日常浏览器、不关闭系统安全机制。本问题与旧 Node `spawn EPERM` 的关系未确认，不能合并为同一已知原因。
