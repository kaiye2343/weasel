# NSIS 可选资源警告仍独立于原生诊断门槛

日期：2026-10-24。状态：记录交接，未修改安装器或打包策略。

## 证据

- 完整 CI [40090582105](https://github.com/kaiye2343/weasel/actions/runs/40090582105) 与 [40106592815](https://github.com/kaiye2343/weasel/actions/runs/40106592815) 均成功，但 NSIS 各有相同七条 `7010 File ... no files found`。
- 缺失匹配：`weaselARM.dll`、`weaselARM64.dll`、`weaselARM64X.dll`、对应三个 `.ime`，以及 `data\*.gram`。
- 报告位置为 `output/install.nsi:247–258` 及 `:298`。NSIS 随后报告 `PASS installer built with 450 unchanged production components`，x86／x64 完整包门槛通过。
- 同轮六份 MSBuild 日志的生产警告、历史负控制警告和错误均为 0；此门槛不包括 NSIS 可选文件提醒。外部 Action Node 20 声明提醒也另行保留。

## 复现与影响边界

在隔离 Actions 按当前完整工作流生成 x86／x64 组件并打包，即可在安装器日志观察。当前证据没有表明 x86／x64 必需组件缺失，也没有 ARM 可安装性验收；不能把成功包装成已提供 ARM 支持。

高级模型需确认可选架构与模型文件的打包契约，再决定按目标架构条件化匹配或保留明确提醒。不要通过全局关闭警告或将可选资源改成无条件必需达成表面清零。本轮按用户规则停止该项修复尝试，不扩大构建范围、不下载模型、不改日常安装。
