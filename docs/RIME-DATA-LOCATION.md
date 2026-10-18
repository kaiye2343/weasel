# 本机 Rime 资料位置（2026-10-18）

此文记录用户授权的本机修复，不将当前用户名或绝对目录写进产品默认设置。其他机器仍使用产品既有资料目录规则。

## 生效位置

| 内容 | 本机路径 |
|---|---|
| Rime 方案、词库、模型、学习数据库、sync | `C:\Users\12877\RimeData` |
| 皮肤包、选择、显示偏好、runtime | `C:\Users\12877\RimeData\appearance` |
| 原资料归档 | `C:\Users\12877\RimeMigrationArchive\20261018-104916` |
| 沿用的已验证安装备份 | `.tools/install-backups/20261016-234724` |
| 私有迁移清单及验证 | `.tools/storage-migration-20261018-104916` |

HKCU `Software\Rime\Weasel\RimeUserDir`（32／64 位）均为新根。设置中心已有读取该注册表项的实现，原生小狼毫亦使用此项，不需要自定义启动脚本或 Codex／DSH 环境变量。开机启动继续使用现有 Program Files 下的程序。

## 为什么保留旧路径

以下位置已不再各自保存一份资料，而是目录联接：

- `%APPDATA%\Rime` → 新 Rime 根。
- Codex 包 `LocalCache\Roaming\Rime` → 同一个新根。
- `%LOCALAPPDATA%\ShorekeeperWeasel` → 新根下 `appearance`。
- Codex 包 `LocalCache\Local\ShorekeeperWeasel` → 同一个 `appearance`。

这是本机兼容入口；真正数据在包缓存外。不要向这些联接执行递归删除或将其误认为四份冗余数据。修改任一入口实际上都会修改同一份资料。原来分叉的实体已移到归档目录，不再参与运行。

## 已验证与待验收

- 原目录冷态复制 531 个文件逐项 SHA256 验证；迁移结束时另核对 167 个未关联配置／资源哈希。
- Rime API 实际加载工作、日常两方案；保留各自数据库。
- 旧全拼通过 Rime 官方快照及词典恢复合入，恢复前后 35 条记录。
- 普通桌面启动服务非管理员，诊断实际根目录正确；工具／桌面两视角一致。
- 没有代理桌面打字测试，也没有重登录／重启电脑测试。实际输入验收由用户完成。

## 回滚原则

迁移事务保留原实体与注册表前值，没有永久删除。回滚应先正常停止小狼毫，保存新根新增的学习快照，保留新根中的全部新数据，再进行路径恢复。

1. 阅读私有记录的 `switch-result.json`、`switch-journal.json` 与 `registry-views-before.json`，核对四个联接、其目标以及四份归档原目录。
2. 仅移除这四个已核对目标的联接本身；禁止递归删除联接目标。将对应归档原目录移回原路径。
3. 恢复注册表原值；原本不存在的项只移除该值，不删除整个 Weasel 注册表键。
4. 新根原样保留。若需将迁移后学习内容带回旧资料，使用 Rime 官方快照／恢复机制，不覆盖活动 `.userdb`。
5. 正常用户上下文启动服务，先读取诊断确认实际资料根，再由用户验收输入。

直接回退旧目录会重新出现两套视图，因此只用于故障回退，不建议作为日常方案。继续安装升级时沿用 `.tools/installation-backup-baseline.json`，其 Backup 仍是原已验证备份，UserRoot 已更新为新根。

官方词典管理接口依据：[librime 1.13.1 rime_levers_api.h](https://github.com/rime/librime/blob/1.13.1/src/rime_levers_api.h)，使用 `restore_user_dict`，与小狼毫传统用户词典管理器相同。
