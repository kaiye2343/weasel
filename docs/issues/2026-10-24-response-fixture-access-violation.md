# C1b 阻塞：响应夹具发生访问违例

## 处理约定

用户于 2026-10-24 先要求问题单交接，随后明确指令“根据 repair 修复”。已按修复指导取得原程序异常指令，定位至未有效初始化实例的空 `clock_()` 调用，并实施有首次构造保护的访问入口；完整 CI 40090582105 的两架构、原失败模式／优化模式及 ABBA 回归已通过；此隔离故障已修复验收，未安装日常环境。前期交接证据保留如下；新取证与实现见 [repair 记录](../REPAIR-EXECUTION-20261024.md)。新的独立问题仍按交接规则处理。

## 摘要与影响

- 阶段：性能诊断 C1b（真实计时点与独立 ABBA 预览）。
- 隔离测试 `response.exe` 编译、链接成功，运行退出码为 `-1073741819`，即访问违例 `0xC0000005`。
- 此失败阻断完整工作流，后续生产构建、ABBA 开销验证和完整打包不能计为本批通过。
- C1c、C2、C3 的依赖门槛未满足；不将性能优化或输入延迟问题记为完成。
- 本机日常输入法没有安装本批构建，没有改动真实 Rime 资料或启动服务。当前证据来自隔离 Actions，不能推断日常环境也发生同一崩溃。

## Git 基线与 CI

| 项目 | 实际记录 |
|---|---|
| 分支 | `shorekeeper` |
| 最后完整成功源码 | `73133f5811ae60da693c347b196872a23f42c710`（C1a） |
| 最后完整成功 CI | [40064217507](https://github.com/kaiye2343/weasel/actions/runs/40064217507) |
| 首次本批失败源码 | `7a5af1026eda860b4fb520a1dc8866cc4a6b3a4c` |
| 失败 CI | [40068466767](https://github.com/kaiye2343/weasel/actions/runs/40068466767) |
| 已推送诊断提交 | `9e258d16183d12fd1df85fda7b03a316f0f41007` |
| 诊断提交 CI | [40069806242](https://github.com/kaiye2343/weasel/actions/runs/40069806242)，状态 failure；后续已取得既有日志，仍为响应夹具访问违例 |
| 独立 D1a 后续 CI | [40082835762](https://github.com/kaiye2343/weasel/actions/runs/40082835762)，D1a 两架构步骤通过，完整工作流再次停在同一响应夹具；未对本问题新增修复或插桩 |

工作区存在用户修改 `tools/SkinFileTrace/README.md`，必须保留，不纳入处理提交。接手时重新检查实际 Git 状态，不重置分支。

该文件的已记录 SHA256：`B60CBA578020934A683AFDD8390EFFF88A4CAB8B8A9E67169E9E3CEF072CAD13`。

## 已取得的失败证据

CI step：`Test upstream response, hover and version adaptations without UI`。

```text
PASS UIStyle archive field order matches installed 0.17.4.64 clients
upstream_response_test.cpp
ResponseParser.cpp
Deserializer.cpp
ActionLoader.cpp
Committer.cpp
ContextUpdater.cpp
Configurator.cpp
Styler.cpp
Generating Code...
Creating library .../current/response.lib and object .../current/response.exp
.../current/response.exe expected 0, got -1073741819
```

首轮日志没有响应夹具的正常输出或 C++ 异常 `FAIL` 输出。该夹具原本仅在全部成功后打印 PASS，**无输出不能证明崩溃发生在 main 之前**。

本机已保存首轮原始失败日志：`.tools/dsh-plan-20261023/ci-40068466767-failed.log`。该目录被 Git 忽略，跨机器接手应从对应 Actions 获取日志，不能依赖本机私有目录运行产品。

第二轮最初获取日志时，GitHub CLI 写入本机日志缓存被拒绝访问。后续通过 GitHub Jobs 日志接口取得既有日志，没有修改缓存权限，也没有追加产品／测试诊断。该工具限制不是 C++ 故障原因。

## 相关实现与复现入口

### 直接相关文件

- `tests/upstream_response_test.cpp`：主测试入口、模拟 Rime API、生产响应方法测试及真实客户端解析；新增采样启动与 15 次响应计数检查。
- `tools/test-upstream-adaptations.ps1`：提取生产方法，构建 current/baseline 两个响应夹具；current 失败后 baseline 尚未执行。
- `include/WeaselPerformance.h`：标准库实现的 Probe、Scope 和进程内 inline `processProbe`。
- `RimeWithWeasel/RimeWithWeasel.cpp`：`_Respond` 第一行新增 `ContextResponse` Scope。
- `.github/workflows/commit-ci.yml`：上述脚本的完整 CI 调用与失败日志归档。

### 隔离测试机制

测试脚本从当前生产源码提取 `_GetCandidateInfo` 和 `_Respond` 相关片段，生成 `response_under_test.inc`。夹具提供模拟 Rime API，链接真实 ResponseParser、Deserializer、ActionLoader、Committer、ContextUpdater、Configurator、Styler；不初始化真实 Rime，不读取用户词库，不操作桌面。

current 使用当前源码；baseline 使用 Git 中 `c8c3332^` 的旧响应方法，并定义 `LEGACY_RESPONSE`。本批原有编译参数为 `/EHsc /std:c++17 /utf-8 /MT /Y-`，链接 Boost 1.84、advapi32 和 user32；响应夹具未指定 `/O2`。CI 使用 Windows 2022、VS 2022 和固定 librime SDK。

在已配置 MSVC、Boost 和 SDK 的**隔离测试环境**中，入口为：

```powershell
./tools/test-upstream-adaptations.ps1
```

本机没有 MSVC；不能把静态审查记作编译或运行通过。当前授权不包括安装实验构建、修改真实配置或驱动日常窗口。

## 本批变化与已核实范围

1. `_Respond` 新增一个 RAII Scope，覆盖读取、构造、序列化及 eat 回调；没有新增 Rime API 调用。
2. 响应夹具新增探针显式启动与结束；只声明 `ContextResponse` 可测，预期成功、空响应与失败回调合计 15 次样本，核心计算仍不可测。
3. `ConfigureApi` 已设置现有响应代码调用的 get/free commit、status、context 及 set_option。未发现本批新增未配置的模拟接口，但尚未通过运行定位排除内存问题。
4. C1a 单独探针测试在 Win32／x64 的 `/O2 /W4 /WX /MT` 条件下均通过；这不能证明当前响应夹具及真实集成一定安全。
5. 虽然响应程序链接多个编译单元，当前列出的解析器编译单元没有直接引入新探针头的路径；不能只凭多编译单元断言 ODR 故障。
6. 静态检查未发现相关结构体 pack 或宏变化。没有证据确认 mutex、CRT、初始化顺序、模拟 API 或解析器中的任一项为根因。

## 已做但未形成结论的诊断

在用户提出“发现问题写 Markdown 交接”之前，已提交 `9e258d1`，仅在响应测试增加 `[response-phase]` 阶段输出，标记 comparisons、optional cursor、probe start/started、模式、首次响应解析完成和 probe finish。

此诊断不改生产行为。后续取得的第二轮日志，以及仅新增 D1a 夹具后的 CI，均出现以下既有标记：

```text
[response-phase] comparisons
[response-phase] optional cursor
[response-phase] probe start
... response.exe expected 0, got -1073741819
```

没有 `probe started`。因此可把范围缩小到两条标记之间，包含探针参数／错误字符串的构造与 `Start` 调用及其检查，**尚不能断言已定位至 Start 内部或确定根因**。没有崩溃栈或更细日志。标记仍保留，后续由接手模型处理；当前执行者不追加诊断或修复尝试。

新增只读取得的原始日志留在本机忽略目录：`.tools/dsh-plan-20261023/ci-40069806242-job.log`、`ci-40082835762-job.log`。跨机器接手从对应 CI 获取，不依赖该本机目录。

## 尚待高级模型决定与处理

- 已取得上述第二轮既有日志；接手时核对最新源码与 CI，避免把阶段区间误当作已确认根因。
- 结合实际证据决定是否需要异常调用栈、编译模式对照或更小夹具；当前列举项均不是已确认原因。
- 在正确调用链上形成可重复失败与修复后的回归，不能通过移除 Scope、跳过测试或单纯增加优化选项掩盖问题。
- 清理临时阶段诊断，保留有价值的失败证据；不得放宽现有测试门槛。
- 完整 CI 通过前，不进入依赖本批成功的缓存优化，也不安装日常环境。

## 交回验收要求

- 写明经证据确认的根因、修改范围与回归结果。
- 响应模式、Unicode／转义、注释、样式、回调失败和资源释放语义仍通过；测量不可伪造或包含输入正文。
- Win32／x64 探针核心及完整原生集成通过；独立 ABBA 的可用阶段、采样时限与最终重绘语义保持准确。
- 保留 IPC/UIStyle 格式、日常资料和用户未提交修改。
- 分别报告自动回归、完整包构建和实机验收；没有运行的项目明确标为未验证。
