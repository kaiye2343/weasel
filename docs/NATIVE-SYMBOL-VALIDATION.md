# 原生符号验证

该检查仅在隔离构建环境运行，不进入输入法、设置中心或按键热路径，不注册系统 COM。

## 入口

在 VS x64 开发环境中运行：

```powershell
./tools/test-native-symbols.ps1 -VerifyProduction
```

需要 Visual Studio 自带的 `DIA SDK`：`dia2.h`、`cvconst.h`、amd64 的 `diaguids.lib` 和 `msdia140.dll`。缺少 SDK、文件或解析能力直接失败；不退化为检查文件存在。验证器显式加载指定 DLL，先释放所有 DIA 对象，再卸载 DLL，最后释放 COM apartment。

## 检查范围

- 复用已有 PE32／PE32+ 读取器，读取实际二进制的 RSDS GUID／age。
- 用 `loadAndValidateDataFromPdb` 验证对应 PDB 身份；不使用嵌入路径自动搜索符号。
- 实际枚举编译单元、函数、源码行和全局 UDT；读取 tag、symbol ID、行号、RVA、源文件 ID 和 UDT 长度。所有类别必须存在有效记录，枚举失败、记录不一致或超出上限均失败。
- 每类记录上限为 500 万，PE 上限 512 MiB；只报告计数和身份，不导出函数名称、输入内容或私人路径。
- UDT 检查覆盖其元数据，不穷尽成员、函数签名和全部类型引用图。因此这是实际符号可读性门槛，不能单独证明所有可能的 PDB 损坏都已排除，也不能据此认定历史 `LNK4020` 根因。

先编译两份独立夹具，再检查错配 PDB、仅修改 RSDS age、缺失 PDB、截断 PDB、截断 PE、缺失及相对路径 DIA DLL。负例必须在预期阶段返回退出码 1；无关失败不算通过。只编译夹具产生调试信息，不运行夹具程序。

正式检查覆盖 11 对组件：x86／x64 服务、部署器、TSF DLL、IME、预览，以及 Win32 安装工具。x64 验证器使用 x64 DIA 读取两种位宽的符号，报告另行检查二进制 machine 字段。

## 产物和验收

全部通过后生成 `output/archives/native-symbols.json`，包括源码号、二进制／PDB 相对路径与 SHA256、GUID／age、实际记录数。匹配的 PDB 保存到 `output/archives/native-symbols`，不进入安装器的 `SettingsCenter` 工具目录。失败夹具和原始 JSON 保留在本次 `.tools/native-symbols-*` 目录，由 CI 失败产物收集。

本机缺少 MSVC，开发阶段的源码审查、PowerShell 语法及工作流绑定检查不代表已经编译通过。阶段 A 还要求实际警告门槛以及两次独立干净 CI；单次成功不能替代它们。

## 接口依据

- [DIA SDK 安装布局](https://learn.microsoft.com/en-us/visualstudio/debugger/debug-interface-access/getting-started-debug-interface-access-sdk)
- [PDB GUID／age 校验接口](https://learn.microsoft.com/en-us/visualstudio/debugger/debug-interface-access/idiadatasource-loadandvalidatedatafrompdb)
- [源码文件枚举](https://learn.microsoft.com/en-us/visualstudio/debugger/debug-interface-access/idiasession-findfile)
- [SymTagEnum 和 cvconst.h](https://learn.microsoft.com/en-us/visualstudio/debugger/debug-interface-access/symtagenum)
