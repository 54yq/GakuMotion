# Motion 数据布局与一次性迁移

`tools/migrate-layout.ps1` 是不依赖 Python 的复制迁移入口。它不会启动游戏、删除旧目录或覆盖不同内容的目标文件。运行时布局由 `PluginData/gakumotion/layout-v1.json` 的有效活动回执选择；回执激活之后，旧目录只作为保留原件，不会再次自动导入覆盖新数据。

## 目标布局

```text
gakumas-local/
  Configs/gakumotion/
    control.json
    bone_overrides.json
    initial_position.json
    camera.json
    sole_measurement.json
    stage.json
  UserData/gakumotion/motions/
    <完整曲目目录，包括未知 custom 文件>
    .gakumotion-trash/
  PluginData/gakumotion/
    playlist.json
    layout-v1.json
    migrations/layout-v1/attempt-<id>.json
  Logs/gakumotion/
    import.log
    camera_import.log
    vmd_import_timing.jsonl
    diagnostics/<新版诊断输出>
    <保留的旧版诊断输出>
  Temp/gakumotion/transactions/
    <由新版导入器管理的同卷事务>
```

迁移完整旧 `motions` 树，包含隐藏恢复区、空目录和未知用户文件。不会只挑选已知扩展名。全局配置及已有 `.bak` 副本进入 Configs；播放顺序、隐藏曲目、播放模式所在的 `playlist.json` 进入 PluginData。诊断请求 `*.request.json` 保留在原位置，不复制到新位置重放；程序 DLL 和旧工具目录不属于用户数据迁移范围。

## 执行入口

先停止游戏及可能修改这些数据的导入/编辑工具，再做独立备份。**本脚本保留原件和复制清单；它不制作独立备份。** 备份、停止游戏及真实安装操作由执行部署的一方负责。

`-LocalRoot` 指向该安装自己的 `gakumas-local`。`-Mode Plan` 为默认模式，逐文件计算 SHA256 并列出来源、目标、复制/相同/冲突状态、事务阻碍和警告。它不创建任何类别目录。只有显式传入 `-PlanPath` 时才写计划文件；该文件必须尚不存在，其父目录须已存在。

```powershell
# 使用调用方已确定的本地根和新的计划文件名。
& tools/migrate-layout.ps1 -LocalRoot $localRoot -Mode Plan -PlanPath $newPlanPath
& tools/migrate-layout.ps1 -LocalRoot $localRoot -Mode Apply
```

Apply 会重新预检并独占本工具的迁移锁，再执行复制，不会盲目执行过时的计划 JSON。先写独立 attempt 清单，复制到同卷临时文件，flush、校验 SHA256、无覆盖 rename；最后重新扫描完整来源并校验每个目标，全部相同时才以无覆盖 rename 原子激活回执。文件被锁定、来源变化、不同内容冲突或链接/重解析点都会失败，旧文件保留。已复制的相同前缀允许重试；不同内容的目标不能由本工具替换。目标曲库中来源没有的额外文件也构成冲突，避免把独立的同名曲库静默合并。

成功激活后重复调用返回 `already-active`，不重新读取旧配置或旧曲库来覆盖目标。回执损坏、属于另一安装或含不同根路径时会明确失败。不要为“恢复兼容”删掉回执后反复迁移；需要排障时先保存回执和 attempt 清单。

以上 CLI Plan 入口及 Apply 核心由合成测试实际执行：

```powershell
pwsh -NoProfile -File tests/test_layout_migration.ps1
```

## 中断与恢复边界

没有活动回执时，产品不会消费部分复制的新布局。进程中断后可重跑 Plan/Apply；已有相同字节可继续，原件仍在。已创建的 attempt 清单及硬中断留下的复制临时文件保留在 `PluginData/gakumotion/migrations/layout-v1`，不会混入扫描曲库，也不自动删除。脚本对每份复制数据和回执显式 flush，但不将其表述为所有文件系统、硬件断电下的事务数据库保证；独立备份仍有价值。

旧根的 `gkmotion-cs-edit-*`，或新 `Temp/gakumotion/transactions/gkmotion-cs-edit-*`，均视为尚未证明安全的编辑事务并阻断迁移。尤其 `original` 可能是唯一可回滚原件，不能当普通 Temp 清理。处理时停止游戏，保留整个事务及 journal，核对目标曲目和哈希，先独立备份两份内容，再恢复已证明的原件或确认保留已完成的目标；将已解决的整个事务归档到 localRoot 之外后重跑 Plan。脚本不会替用户猜测应选哪份内容，不自动恢复、移动或删除事务。新导入的 `gkmotion-cs-*` 残留会报告且保留，不自动当作已完成曲目发布。

## 路径与配置语义

类别根使用当前安装内的固定 game-local 路径；脚本拒绝文件系统卷根、目录链接和 junction，不扫描整个磁盘或外部素材目录。`overrides.global_file`、`overrides.motion_file` 的旧相对锚点由运行时兼容逻辑处理，本工具不会解析或改写用户配置内容。曲目内 `info.json` 的原素材来源记录与源副本一起按字节保留，不为整洁扫描、搬走或重命名外部原素材。

本轮迁移不改变动作求值、轨道格式、曲库删除/恢复语义，也不提供卸载当前分支后回到历史布局的反向迁移。

运行时配置、曲库和日志路径使用 UTF-8，Windows 文件接口使用原生宽路径。音频仍使用 waveaudio/MCI；部分旧驱动对特殊 Unicode 路径有限制，新版只读尝试同一文件已有的短路径别名，无可用别名时保留明确的 MCI 失败，不复制或改名用户音频。

`tools/new-layout-smoke-track.ps1` 可在一个尚不存在的显式输出目录生成自有的 2 秒中立合成曲目，用于临时曲库的加载、播放和停止验证。它含静止根与 Hips 单位旋转，没有音频、镜头或第三方素材；输出目录已存在时拒绝覆盖。实机放入测试库由负责部署的一方执行，生成脚本不搜索游戏目录。
