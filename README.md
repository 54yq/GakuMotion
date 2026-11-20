<div align="center">
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="assets/branding/gakumotion-wordmark-dark.svg">
  <img alt="GakuMotion" src="assets/branding/gakumotion-wordmark-light.svg" width="480">
</picture>
<p>A Native Performance Extension for Gakumas</p>
</div>

GakuMotion 是《学园偶像大师》的 GakuLoader 演出插件，用游戏原有角色、服装和渲染环境播放外部 MMD 动作、表情、镜头与音乐。此分支由[野生的空白（54yq）](https://github.com/54yq)维护，基于 [crested-ezios/GakuMotion](https://github.com/crested-ezios/GakuMotion)。

**当前分支为 v4.3.16-web，加载器显示 `v0.2.0+fork.4.3.16-web`。** Live 接管、原生物理与风场仍属于实验功能，表现取决于游戏版本、人物和服装。[分支更新记录](docs/CHANGELOG.md) · [Web 看板说明](docs/WEB_PANEL_BRIDGE.md)

[上游安装指南](https://github.com/crested-ezios/GakuMotion/blob/main/docs/INSTALLATION.md) · [上游使用指南](https://github.com/crested-ezios/GakuMotion/blob/main/docs/USAGE.md) · [上游常见问题](https://github.com/crested-ezios/GakuMotion/blob/main/docs/TROUBLESHOOTING.md)

## 基础功能

基础动作功能适用于支持的拍摄及指定更衣室场景。

| 功能 | 说明 |
| --- | --- |
| 动作与表情 | 导入 MMD/VMD 身体动作并重定向到游戏角色，可添加表情、眼动等轨道。 |
| 镜头 | 播放 VMD 运镜，跟随角色、固定机位注视或交回游戏手动控制。 |
| 音乐与时间轴 | 播放 16-bit PCM WAV。动作、表情、镜头和音乐各自可设置速度与偏移，支持暂停、循环、跳转和逐帧预览。全局倍速与音乐文件速度的乘积不为 1.0 时，音乐静音。 |
| 舞台与物理 | 调整支持场景的背景与舞台表现，可选内建次级物理或 GakuBurstBridge 原生物理。 |

### 已合入上游的改进

下列贡献已合并到上游 `dev`。上游发布包的可用性以对应版本为准。

| 改进 | 内容 | PR |
| --- | --- | --- |
| 表情幅度与音乐音量 | 独立调整整体、眉毛、眼部和嘴部倍率，以及 Motion 自身音乐音量。 | [#2](https://github.com/crested-ezios/GakuMotion/pull/2) |
| 运镜范围与辅助骨 | 缩放 VMD 镜头轨迹，统一辅助骨计算中的等价四元数符号。 | [#4](https://github.com/crested-ezios/GakuMotion/pull/4) |
| 导入帧率 | 将源 VMD 的自动、30 或 60 fps 与输出采样率分开设置。已有曲目可重新导入。 | [#9](https://github.com/crested-ezios/GakuMotion/pull/9) |
| 烘焙腿部 | 识别特征明确的 D 腿骨及空 IK 配合逐帧烘焙的腿部轨道。重导曲目后应用。 | [#10](https://github.com/crested-ezios/GakuMotion/pull/10) |
| 场上角色名称 | 校正角色显示名，并读取 GakuActors 提供的自定义预设名。 | [#11](https://github.com/crested-ezios/GakuMotion/pull/11) |

## 分支扩展

### Web 看板

使用支持插件 WebView2 面板的 GakuLoader 打开动作、多人站位与舞台控制。底部播放栏可预览动作第一帧。多人卡片支持交换已绑定的副角色，各槽位保留原有动作和时间偏移。缺少眼睑轨道的动作在 Live 接管中保留自然眨眼。

在 **舞台 → 舞台视觉控制** 可独立切换 Paraffin 镜头光晕与 Live 景深，并读取或调整角色固定打光。VMD 镜头默认自动关闭 Live 景深，支持在当前场次手动覆盖。时间轴道具可按组或网格调整显隐。

### 文件夹曲库与多人同步

1. 在 **曲库 → 添加文件夹** 选择动作目录。Motion 会扫描子目录，读取已有转换产物，并将待转换的身体 VMD 放入管理缓存。可按来源文件夹筛选。移除文件夹来源后，外部源文件和转换缓存仍可使用。
2. 在 **多人同步播放** 中按需添加槽位，为场上角色各自绑定动作。停止播放后也可使用 **按名称自动分配**，按同目录编号或字母后缀分配分轨。新增场上人物可以使用 GakuActors。
3. 在 **站位与朝向** 调整各角色的世界位置和朝向。原曲队形可以按主角色朝向对齐，需要校验匹配的原始 VMD。手动调整过的站位可继续微调；多人站位预设界面暂时隐藏。
4. 音乐、镜头和进度以主角色为准。附加角色各自拥有动作、延迟及骨骼覆盖。配合支持 V3 的 GakuBurstBridge 可使用独立原生物理上下文，具体后端可在界面确认。

[文件夹曲库与自动分配](docs/FOLDER_LIBRARY.md) · [GakuActors](https://github.com/54yq/GakuActors) · [多人槽位上游 PR #8](https://github.com/crested-ezios/GakuMotion/pull/8) · [Bridge 兼容说明](tools/GakuBurstBridge/docs/COMPATIBILITY.md)

### Live 观看、动作接管和场景道具

| 功能 | 用法 |
| --- | --- |
| 控制原生 Live | 查看进度，暂停、继续或拖动跳转。可设为结束前 0.5–3 秒从头播放，默认提前 1 秒。启用接管或进入拍照保护时暂停这项循环。 |
| 动作接管 | 确认场景和角色已识别，再手动启用。接管后以外部时间轴驱动动作，退出时恢复原角色和 Live 状态。 |
| 角色图与物理 | 兼容条件满足时，将原 Live 动画片段保持在结尾附近，并保持角色图推进，以继续驱动头发和服装的原生 Swing。切曲、暂停和拖动进度时保留同一角色的片段锁定。不同组合的外观和恢复效果需实机确认。 |
| 场景道具 | 进入支持场景后自动扫描，在 **动作 → 场景道具** 查看麦克风、行李箱、荧光棒、水枪、糖果和其他符合规则的项目。Live 接管后可切换显隐。行李箱的模型节点与对应网格分别恢复。 |
| 性能观察 | 在接管界面查看 Unity 更新频率、帧间隔和卡顿计数，可选用日志检查异常。 |

接管默认关闭，适用于支持的 Live 观看场景。游戏主页、养成和未适配页面不开放接管。帧间隔反映 Unity 更新节奏，画面输出和卡顿原因需另外分析。[Live 角色图与接管](docs/LIVE_ACTOR_CLOCK_MVP.md) · [Live 循环](docs/LIVE_STAGE_CLOCK_SPLIT.md)

### 镜头、音效与骨骼校正

- **镜头与舞台**支持镜头随主角朝向、Live VMD 相机对齐及自由视角交接。景深和风场调整为实验选项。
- **游戏音效**可单独屏蔽 Se（含界面音效）。开关默认关闭，离开支持场景或进入受保护的拍照流程时尝试恢复原状。来源为 huvz04 的上游 [PR #13](https://github.com/crested-ezios/GakuMotion/pull/13)，该 PR 已关闭，本分支保留实现。
- **骨骼校正**包括 gkclip 局部骨骼参考帧修正、按角色调整前臂扭转与轴向，以及辅助骨实验对照。图形界面显示当前角色与选用的校正方式。

## 功能演示

下列视频由野生的空白发布，同时展示部分基础修正。

| 视频 | 主要展示 |
| --- | --- |
| [多人同步演出](https://www.bilibili.com/video/BV1otWa62EgK) | 多名角色分别绑定动作并同步演出。 |
| [导入人物并接管舞台](https://www.bilibili.com/video/BV1G6s76KE3C/) | 通过 GakuActors 加入人物，再使用 GakuMotion 接管舞台。 |
| [舞台接管与超出三人的多人演出](https://www.bilibili.com/video/BV14Ds76bE4e) | 使用额外人物槽位演出。 |

## 上游 PR 说明

状态核对于 2026-11-20。动态多人槽位 [#8](https://github.com/crested-ezios/GakuMotion/pull/8) 仍向上游 `dev` 开放。早期综合提交 [#6](https://github.com/crested-ezios/GakuMotion/pull/6) 和首版腿骨修复 [#7](https://github.com/crested-ezios/GakuMotion/pull/7) 均已关闭且未合并，腿骨修复后续以 [#10](https://github.com/crested-ezios/GakuMotion/pull/10) 合入。各 PR 的改动范围以其页面为准。

## 文档与反馈

安装与通用快捷键请参阅页首的上游指南。与 VR 模组共用时，请参阅[兼容性说明](docs/USAGE.md#已知兼容性)。分支源码可分别构建运行时与 NativeAOT 导入器。构建输出和版本说明见 [构建指南](BUILD_NOTES.md) 与 [分支使用说明](docs/USAGE.md)。

反馈时附上插件、加载器和游戏版本、场景、角色与服装、复现步骤及相关日志。[分支 Issues](https://github.com/54yq/GakuMotion/issues) · [上游已知问题](https://github.com/crested-ezios/GakuMotion/blob/main/docs/KNOWN_ISSUES.md)

## 相关项目

- [GakuLoader](https://github.com/Gaku-Mod-Tools/GakuLoader) 提供插件宿主、按键与界面服务。
- [GakuActors](https://github.com/54yq/GakuActors) 提供人物加载、头身预设、资源库与场景管理。
- [KineTexture](https://github.com/crested-ezios/KineTexture) 提供运动学、数学计算与采样能力。
- [GakuBurstBridge](https://github.com/crested-ezios/GakuBurstBridge) 为原生 Burst 物理提供动作输入。[分支配套源码](tools/GakuBurstBridge/README.md)

- [GakuModTools Mod-Directory](https://github.com/Gaku-Mod-Tools/Mod-Directory) 收录学园偶像大师 PC 版的模组、插件与开发工具。

项目源码采用 [MIT](LICENSE)。第三方组件遵循各自许可，参考工具及贡献者见 [第三方通知](THIRD_PARTY_NOTICES.md)。

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
