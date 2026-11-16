<div align="center">
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="assets/branding/gakumotion-wordmark-dark.svg">
  <img alt="GakuMotion" src="assets/branding/gakumotion-wordmark-light.svg" width="480">
</picture>
<p>A Native Performance Extension for Gakumas</p>
</div>

GakuMotion 是运行在 GakuLoader 之上的《学园偶像大师》原生演出扩展，使用游戏内的角色、服装、材质与渲染环境播放外部 MMD 动作、表情、镜头和音乐。

**本仓库是 [crested-ezios/GakuMotion](https://github.com/crested-ezios/GakuMotion) 的功能扩展分支**，主要围绕多人同步演出、Live 观看场景接管、角色站位与运镜适配，以及动作导入和校正展开。本 README 先介绍上游基础能力，并单独标出由本分支贡献、已经回馈上游的功能，再说明本分支的其他扩展。

本分支由[野生的空白（54yq）](https://github.com/54yq)维护。下方附有 B 站功能演示。

[上游项目](https://github.com/crested-ezios/GakuMotion) · [上游安装指南](https://github.com/crested-ezios/GakuMotion/blob/main/docs/INSTALLATION.md) · [上游使用指南](https://github.com/crested-ezios/GakuMotion/blob/main/docs/USAGE.md) · [上游常见问题](https://github.com/crested-ezios/GakuMotion/blob/main/docs/TROUBLESHOOTING.md)

> 项目仍处于开发预览阶段。下文按本分支当前代码与合入历史区分功能来源；上游后续也可能合入相同能力。Live、原生物理和标有“实验”的功能仍需按游戏版本、角色、服装与场景分别验证。

## 上游基础功能与本分支已回馈的改进

以下为上游原有的基础能力：

| 功能 | 能力 |
| --- | --- |
| 动作回放 | 导入 MMD/VMD 身体动作，使用游戏原生角色进行重定向与播放。 |
| 面部表现 | 播放表情、眼动及相关面部轨道，具体效果取决于素材和转换支持。 |
| 镜头控制 | 支持 VMD 运镜、角色跟随和固定机位注视，也可将镜头控制交还游戏。 |
| 音频同步 | 同步播放 16-bit PCM WAV，支持动作、表情、镜头与音乐各自的速度和时间偏移。 |
| 曲库与播放控制 | 管理曲目，预览首帧，播放、暂停、停止、循环、跳转、逐帧查看及调节全局倍速。 |
| 舞台与物理 | 在支持的场景中调整背景和舞台表现；提供内建次级物理及可选的 GakuBurstBridge 原生物理后端。 |

以下改进已通过 PR 合入上游 `dev` 分支：

| 已合入上游的功能或修复 | 改进内容 | 上游 PR 与合入位置 |
| --- | --- | --- |
| 表情幅度控制 | 增加整体、眉毛、眼部和嘴部倍率，支持重置与保存。 | [#2](https://github.com/crested-ezios/GakuMotion/pull/2)，已合入 `dev`。 |
| 独立音乐音量 | 增加 0–100% 音乐音量及保存功能，并处理旧音频后端的兼容回退。 | [#2](https://github.com/crested-ezios/GakuMotion/pull/2)，已合入 `dev`。 |
| VMD 运镜范围控制 | 增加 0.30–1.00 倍轨迹缩放、镜头设置保存，并同步调整焦点距离和正交视野。 | [#4](https://github.com/crested-ezios/GakuMotion/pull/4)，已合入 `dev`。 |
| 辅助骨旋转符号修正 | 统一等价四元数的符号，修复相同旋转因符号不同而得到不同辅助骨输出的问题。 | [#4](https://github.com/crested-ezios/GakuMotion/pull/4)，已合入 `dev`。 |

“已合入 `dev`”表示上游开发分支已接受该 PR，不代表上游 `main` 或正式发布包已包含。下文保留这些功能的说明，并在对应位置标出已回馈的 PR。

基础动作功能沿用上游的摄影及指定更衣室场景支持。安装、资源准备、快捷键和通用操作请参阅上方的上游文档；本页集中说明分支差异。

## 功能演示

以下为野生的空白发布的 B 站演示：

| 演示 | 内容 |
| --- | --- |
| [多人同步演出](https://www.bilibili.com/video/BV1otWa62EgK) | 多名角色分别绑定动作并同步演出。 |
| [导入未实装人物并接管舞台](https://www.bilibili.com/video/BV1G6s76KE3C/) | 配合 GakuActors 导入人物，再使用 GakuMotion 接管舞台。 |
| [舞台接管与超出三人的多人演出](https://www.bilibili.com/video/BV14Ds76bE4e) | 接管舞台并导入多名人物，展示超出原三人上限的演出。 |

人物资源导入使用配套插件 [GakuActors](https://github.com/54yq/GakuActors)，动作播放与 Live 接管由 GakuMotion 负责。实际效果取决于游戏版本、资源与插件组合。

## 分支功能与扩展

### 多人同步演出

动态槽位与多人原生物理已提交上游 [PR #8](https://github.com/crested-ezios/GakuMotion/pull/8)，尚未合并。可查看[多人演出](https://www.bilibili.com/video/BV1otWa62EgK)和[超出三人的舞台演出](https://www.bilibili.com/video/BV14Ds76bE4e)演示。

- **动态角色槽位**：从早期三人同步扩展为按需新增角色槽位，为已检测到的场景角色分别绑定曲库动作。未绑定的槽位跳过，播放、暂停、进度、音乐和镜头统一以主角色为准。
- **每人独立设置**：附加角色分别使用自己的身体动作、骨骼覆盖、动作延迟、位置和朝向；表情幅度沿用面板设置。同一场景内切换动作或重新绑定时保留对应角色的站位。
- **还原原曲队形**：读取各曲目原始 VMD 第 0 帧的横向相对站位，在主角色坐标与朝向下对齐队形，并保留主角色站位和附加角色的垂直偏移。需要 gkclip 曲目及校验匹配的原始 VMD；条件不足时不修改队形。
- **多人原生物理适配**：配合支持 V3 接口的 GakuBurstBridge，为主角色和已绑定的附加角色分别管理原生物理上下文，并显示实际后端；对应角色的原生输入不可用时回退到内建物理。本仓库附带[配套 Bridge 源码](tools/GakuBurstBridge/README.md)。

角色槽位用于绑定场景中已有的人物，不会创建人物；实际可用人数受场景角色、资源与性能限制。修改绑定需要先停止播放，切换场景或角色名单变化后可能需要重新绑定。

### 角色站位与显示控制

- **独立位置与朝向**：主角色和附加角色可分别调整世界坐标偏移与原地朝向。转向同时旋转身体姿态和动作位移轨迹，方便把整支舞放到合适的位置；主角色初始站位可保存为下次启动的默认值。
- **角色模型显隐**：可分别隐藏主角色或已绑定附加角色的模型，并恢复之前的渲染状态；Live 接管中还可分别隐藏麦克风。
- **外部人物识别**：可识别受支持的 GakuActors 新增人物，并在角色列表中显示其自定义预设名称，便于区分多人组合。人物创建和头身组合由 [GakuActors](https://github.com/54yq/GakuActors) 提供，动作绑定由本分支负责；参见[人物导入与舞台接管演示](https://www.bilibili.com/video/BV1G6s76KE3C/)。角色名称修正与预设名显示已提交上游 [PR #11](https://github.com/crested-ezios/GakuMotion/pull/11)，尚未合并。

多人站位预设的界面目前隐藏，已有预设文件保留；当前可用的队形功能为上面的“以主角色对齐原曲队形”。人物显隐与麦克风识别依赖游戏接口和场景资源，不能保证所有模型均适用。

### Live 观看与动作接管（实验）

上游主线默认不开放 Live 演出接管。本分支增加独立的 Live 接管页面，在确认观看场景、角色和拍照保护就绪后，显式启用以下能力：

| 功能 | 本分支的行为与边界 |
| --- | --- |
| Live 播放控制 | 查看原生 Live 进度，暂停、继续和拖动跳转；动作接管期间禁用原生进度操作。 |
| MMD 动作接管 | 暂停原演出舞台后进入 MMD 曲库，使用已识别角色播放外部动作，也可使用多人绑定。退出时先恢复角色，再按接管前状态恢复 Live。 |
| 结束前自动循环 | 可在结束前提前跳回开头，默认提前 1 秒，可调 0.5–3 秒；暂停、接管、拍照或手动跳转期间不触发，失败或未确认回跳时关闭。它不拦截结束回调，也不保证音乐、特效无缝循环。 |
| 角色图独立推进 | 在满足角色归属和原生物理条件时，使角色动画图在舞台暂停期间继续推进，供 MMD 接管使用；结束时恢复图的时间模式、播放状态和暂停节点。属于实验路径，不等同于完整物理快照恢复。 |
| 帧数与卡顿监测 | 接管页内置 Unity 更新频率、帧间隔、近期基准和卡顿计数，并记录排查线索；这是 Unity 更新频率，不是显示器输出帧率。 |

Live 接管默认关闭，游戏主页、养成和未适配页面仍不开放动作接管。不同 Live、角色、服装、多人组合和长时间运行的恢复效果需要分别确认。曾导致黑屏的结束回调重播路径保持禁用。

当前角色图推进与恢复的验证范围见 [Live 接管与角色图](docs/LIVE_ACTOR_CLOCK_MVP.md)，提前循环的机制与限制见 [Live 循环说明](docs/LIVE_STAGE_CLOCK_SPLIT.md)。帧数监测已集成，使用本版时不需要另装旧版 `GakuFrameMonitor.dll`。

### 镜头适配

- **VMD 运镜范围缩放（已通过 [PR #4](https://github.com/crested-ezios/GakuMotion/pull/4) 合入上游 `dev`）**：在 0.30–1.00 倍间缩小轨迹，以角色附近为中心调整运镜范围；保留镜头朝向和视野，并同步处理焦点距离及正交视野。
- **镜头随主角色朝向**：使插件镜头的位置与方向跟随主角色站位朝向旋转，方便调整舞蹈面对的方向；可保存镜头设置。
- **关闭 Live 景深（测试）**：接管期间提供景深渲染开关，取消勾选或退出接管后恢复原处理；接口启用与实际画面效果需分别确认。
- **自由视角交接**：与配套 GakuActors 协调相机控制，开启其自由视角时释放 Motion 镜头，关闭后交还。自由视角的操作入口和实现由 GakuActors 提供。

运镜范围缩放不会自动检测碰撞，也不保证避免穿模或获得正确景深；上述朝向选项不改变游戏手动镜头。

### 动作导入与兼容修正

- **区分源帧率和输出采样率**：导入及管理曲目时可选择自动、30 或 60 fps 的源动作帧率，并单独设置输出采样率。源帧率决定素材的时间基准，输出采样率决定保存密度；已有曲目可指定参数重新烘焙。
- **改进自动帧率判断**：参考身体骨骼与音频判断时间基准，减少少量晚结束的表情轨道对判断的影响；表情末帧明显晚于身体时显示提示，保留原关键帧。上述源帧率、输出采样率与自动识别改进已提交上游 [PR #9](https://github.com/crested-ezios/GakuMotion/pull/9)，尚未合并。
- **兼容烘焙腿部动作**：识别特征明确的 `D` 腿骨，以及普通腿骨逐帧烘焙配合空 IK 轨道的 VMD，保留烘焙后的腿部姿态。显式 IK 开关、实际运动的 IK 轨道和无法明确识别的素材沿用原有处理。对应上游 [PR #10](https://github.com/crested-ezios/GakuMotion/pull/10)，已提交、尚未合并。
- **修正局部骨骼覆盖的帧计算**：修复 gkclip 局部骨骼微调使用的参考帧问题。

自动帧率判断仍可能需要人工调整。导入器更新不会自动重写已有曲目；应用腿部兼容修正需要重新导入原始 VMD。

### 表情、音量与手臂校正

| 功能 | 本分支新增的控制 |
| --- | --- |
| 表情幅度（已通过 [PR #2](https://github.com/crested-ezios/GakuMotion/pull/2) 合入上游 `dev`） | 分别调整整体、眉毛、眼部和嘴部倍率（0.00–2.00），支持重置与保存；不改变眼骨视线或 LookAt。 |
| 独立音乐音量（已通过 [PR #2](https://github.com/crested-ezios/GakuMotion/pull/2) 合入上游 `dev`） | 将 GakuMotion 播放的音乐调整为 0–100%，松开后自动保存，不改变游戏原声音量；旧音频后端可能不支持独立调节。 |
| 按角色校正前臂（实验） | 为检测到的角色分别调整前臂扭转分担、轴向角度和锁定状态；尚未绑定动作的角色也可预设。主角色锁定可保存，其他角色设置在当前场景内保留，适用于 gkclip。 |
| 辅助骨旋转符号修正（已通过 [PR #4](https://github.com/crested-ezios/GakuMotion/pull/4) 合入上游 `dev`） | 使表示同一旋转的 q 与 -q 得到一致的辅助骨输出；不改写动作文件或主骨骼旋转。 |
| 手臂辅助骨对照（实验） | 高级动作设置提供拟合、锁定初始姿态、归零、录制回放和旧版解算等模式，可用 F8 切换，用于比较手臂变形。 |
| 载入前初始化骨骼（实验） | 可在下一次载入动作时重置各角色动画器，用于对照；可能改变站位或物理状态。 |

前臂校正默认轴向为 +45°、扭转分担为 0，可解锁后调整和重置。实验校正的效果取决于模型和动作，并不自动解决所有穿模。音乐仍沿用上游限制：全局倍速与音乐文件速度的乘积不为 1.0 时静音。

### 插件协作与诊断

- **GakuActors 资源交接**：提供资源显隐控制的交接接口，允许配套版本在恢复并释放 Motion 所控制的资源后接手，减少两个插件同时修改资源的冲突。
- **物理状态监控**：区分原生物理、内建回退、等待和故障，显示匹配、写入、失败及角色图状态，帮助判断当前实际使用的路径。
- **Live 与手腕诊断**：提供只读 Live 状态记录、Swing 报告、手腕姿态记录、可选的连续手臂日志，以及“角色图运行 3 秒”等测试入口。连续诊断可能影响性能，计数与报告也不能单独证明画面或物理效果正确。
- **构建兼容修正**：修复便携式 Runtime 构建在包含空格的路径下的问题；本分支构建入口与配套组件说明见 [BUILD_NOTES.md](BUILD_NOTES.md)。

### 从其他贡献者的上游 PR 引入的改动

**屏蔽游戏音效**来自贡献者 huvz04 提交的[上游 PR #13](https://github.com/crested-ezios/GakuMotion/pull/13)，已整合到本分支的舞台页。截至 2026-11-17，该 PR 仍为草稿、尚未合并到上游。它可独立关闭游戏 Se 音效（包括界面音效），不影响动作歌曲；关闭开关、离开支持场景或进入受保护拍照流程后恢复先前静音状态，每次启动默认关闭。

该功能归功于对应 PR 的贡献者，与上面的本分支回馈记录分别列明来源。

## 本分支向上游提交的 PR

以下为[野生的空白（54yq）](https://github.com/54yq)提交的上游 PR，状态核对于 **2026-11-17**。本分支中的功能实现与面向上游的移植版本可能存在差别，功能详情以各 PR 的实际范围为准。

| PR | 功能或修复 | 上游状态 |
| --- | --- | --- |
| [#2](https://github.com/crested-ezios/GakuMotion/pull/2) | 表情幅度与独立音乐音量控制。 | **已合入 `dev`**。 |
| [#4](https://github.com/crested-ezios/GakuMotion/pull/4) | VMD 运镜范围控制、辅助骨旋转符号修正。 | **已合入 `dev`**。 |
| [#8](https://github.com/crested-ezios/GakuMotion/pull/8) | 动态多人槽位与独立原生物理上下文；不包含 Live 接管和帧数监测。 | 已提交，尚未合并。 |
| [#9](https://github.com/crested-ezios/GakuMotion/pull/9) | 区分源动作帧率与输出采样帧率，改进自动识别并支持重新烘焙。 | 已提交，尚未合并。 |
| [#10](https://github.com/crested-ezios/GakuMotion/pull/10) | 独立修复烘焙 VMD 腿骨导入后折叠，重新基于上游 `dev` 提交。 | 已提交，尚未合并；接续 #7。 |
| [#11](https://github.com/crested-ezios/GakuMotion/pull/11) | 修正角色显示名称，展示可确认的 GakuActors 自定义预设名。 | 已提交，尚未合并。 |
| [#6](https://github.com/crested-ezios/GakuMotion/pull/6) | 早期多人体验、队形、手臂校正、Live 接管与腿骨修复的综合提交。 | 已关闭，未整体合并；维护者曾讨论按模块择取，不能将整项视为已被上游接受。 |
| [#7](https://github.com/crested-ezios/GakuMotion/pull/7) | 从 #6 拆出的独立腿骨导入修复。 | 已关闭、未合并，后续以 #10 重新提交。 |

## 文档与反馈

通用安装、使用和排障由上游文档维护；分支新增功能的操作补充见[本分支使用说明](docs/USAGE.md)。源码构建请使用[本分支构建说明](BUILD_NOTES.md)，配套多人原生物理请参阅 [Bridge 兼容说明](tools/GakuBurstBridge/docs/COMPATIBILITY.md)。

分支功能问题请提交到[本仓库 Issues](https://github.com/54yq/GakuMotion/issues)，注明本分支版本、GakuLoader 与游戏版本、所在场景、角色和服装、复现步骤及相关日志。通用限制另见[上游已知问题](https://github.com/crested-ezios/GakuMotion/blob/main/docs/KNOWN_ISSUES.md)。

## 相关项目

### 依赖项目

- [GakuLoader](https://github.com/Gaku-Mod-Tools/GakuLoader)：提供插件宿主、按键分发与控制窗口支持。
- [GakuActors](https://github.com/54yq/GakuActors)：可选的人物与场景资源管理插件，用于导入额外人物、管理头身组合及场景资源。
- [KineTexture](https://github.com/crested-ezios/KineTexture)：提供底层正向运动学、数学计算与动作采样能力。
- [GakuBurstBridge](https://github.com/crested-ezios/GakuBurstBridge)：可选的原生物理伴生库，用于让官方 Burst 物理读取动作输入。
- 模组源码遵循 [MIT 许可证](LICENSE)，第三方组件遵循各自许可证，详见 [第三方通知](THIRD_PARTY_NOTICES.md)。
### 参考项目与致谢

GakuMotion 的开发过程中参考了 MMD 动作生态、游戏 Mod 工具及相关研究成果。感谢以下为动作语义验证、兼容性研究和技术设计提供的帮助。
 - [Chinosk - Gakumas Localify](https://git.chinosk6.cn/chinosk/gkms-localify-dmm) — 原生插件兼容生态。
 - [KagaminTheMirror - Gakumas VRify](https://github.com/KagaminTheMirror/gakumas-VRify) — VR 兼容生态。
 - [三目YYB - YYB Hatsune Miku_Default](https://www.bilibili.com/video/BV1Bg411D7Me/) — GakuMotion VMD Source Evaluator 研究中的重要 MMD/PMX 参考模型。
 - [樋口優 - MikuMikuDance](https://www.nicovideo.jp/watch/sm2420025) — VMD 的原始执行环境。
 - [Mogg - MikuMikuMoving](https://sites.google.com/site/mikumikumovingeng/) — 另一个独立的 MMD 动作执行环境，作为动作表现验证。
 - [powroupi / 后续维护者 - blender_mmd_tools](https://github.com/powroupi/blender_mmd_tools) — MMD 数据与行为研究参考。
 - [Countd360 - MMD Director](https://www.patreon.com/posts/mmd-director-pv-55604378) — HS2/AI/KK/KKS CharaStudio的 VMD 播放与编排工具先例。
 - [ifui小灰灰 / ArinLee](https://arin3000.github.io/) — 学马仕动画与原生表现相关研究。
 - [nijinekoyo - Gakuen-idolmaster-ab-decrypt](https://github.com/nijinekoyo/Gakuen-idolmaster-ab-decrypt) — 提供了版本研究思路。
 - [Akisaira](https://github.com/Akisaira) — 援助了早期兼容性研究的智能体调用额度。

配套源码：[GakuBurstBridge 原生物理桥](tools/GakuBurstBridge/README.md)。
