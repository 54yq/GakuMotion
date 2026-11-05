# 源码快照与来源记录

## 当前源码范围

当前源码恢复了固定原仓 `2b28931e8b50b780668baec5e3982927667c3e08` 中原先缺省的11个必需文件，原始 Git 字节与此前记录的 SHA256 一致。包括作者拟合/校准参数、少量实测鞋底坐标与索引、92骨静态矩阵及两份带测量常量的实现文件；不包含原模型、贴图、动作、音频、录制正文或训练输入。

用户已选择按上述明确范围恢复完整候选。本次保持算法和参数不变，只修复独立源码包的构建接入：移除本轮 Agent 新加的审核/哈希构建门禁，补上稳定 .NET 8 SDK 选择；没有把来源记录作为普通编译条件。可选快照核验在 `tools/verify-source.ps1`，生产构建不读取本页或清单状态。

## 2026-11-02 产品路径更新

本轮从活动研发仓固定 `102df32c515e49d78f086c9467ec591a93716442` 导出分类路径、复制迁移、Unicode 文件接口及其现有舞台配置/界面依赖闭包。新增路径 SDK、迁移工具和自有合成测试；没有纳入 DevAgents、research、artifacts、真实素材、未接线的启动探针或并行牙齿研究。11 个已恢复输入的字节和原来源身份全部保留。

根 `build.ps1 -Target All`、稳定 .NET 8 的 `global.json`、冻结字节用的 `.gitattributes` 与产品首页结构继续保留。来源映射和每个文件的当前身份写入 `freeze-manifest.json`；旧来源、旧字节身份放入相应条目的历史记录。它们不成为构建审批条件。

本轮只做本地构建/快照更新，不部署游戏或发布远端。活动源码已有的路径/迁移合成测试和部署/实机证据与此次 staging 构建分别记录，不声称重新编译的 DLL 必须逐字节复现旧产物。

## 历史：初次部分源码候选

以下保留 `e4a9864d01e9773053fa3eaf7ed9bd93d2847e5c` 及文档恢复阶段的选取记录，描述当时的缺项和测试边界；不代表当前仍缺这11项，也不是新的产品构建限制。

冻结源提交：`2b28931e8b50b780668baec5e3982927667c3e08`。
本次只整理源码，不改原仓，不改算法，不部署，不操作真实素材。

### 已纳入

原生程序与接口按固定 Git blobs 导出，保留原始字节；MinHook 另保留完整许可。
NativeAOT 的 14 个生产 C# 文件、项目文件及必要 SDK 配置已选取。
原冻结 Candidate 共 31 项，本候选只纳入其中 26 个 C# 与项目文件；其余 4 项在下表中明确排除。
原冻结哈希所证明的是源码身份，不是这些数据的分发许可。

两个产品 JSON 经内容核对可收录：`gakumas_morph_map.json` 只有接口名字、别名映射和文本来源说明；
`vmd_eye_profile.json` 只有三个轨道名字、左右眼 HID 与语义声明。没有把 JSON 扩展名整体当作数据禁入，
也没有据此延伸批准 MMDD 的原 config、源码或几何。

KineTexture 的源头、许可证与头文件一致性已核对。Dear ImGui 的本地源字节被冻结，
其声明上游 commit 的完整对象关联没有本地证据；清单保留该边界。
代码与依赖选取不构成对整个研发仓、所有历史贡献或全网原创性的保证。

### 未纳入且阻止完整构建的必需输入

| 路径 | 已证实性质 | 受影响目标 |
| --- | --- | --- |
| `src/GkAuxChannelModel.hpp` | 由 4515 帧官方录制拟合的经验模型系数 | Runtime |
| `src/GkSoleProxyProfile.hpp` | 官方鞋 mesh 派生 facet 与中心参数 | Runtime |
| `src/GkSolePitchEnvelope.hpp` | BakeMesh 原顶点索引与坐标 | Runtime |
| `src/GkEyeTargetFrames.hpp` | 官方 face-prefab／eye-sheet 几何派生增益 | Runtime |
| `src/GkFacialAdapterModel.hpp` | 官方 face/default-animation 权重与经验先验混合 | Runtime |
| `candidate/csharp/src/GakuMotion.VmdImporter/Output/FrameAlignment.cs` | 程序代码中混入真实 YYB/MMD 测量方向 | NativeAOT |
| `candidate/csharp/src/GakuMotion.VmdImporter/Rig/VirtualMMDRig.cs` | 程序代码中混入静态脚趾几何默认 | NativeAOT |
| `candidate/csharp/src/GakuMotion.VmdImporter/Resources/gakumas_bone_profile.json` | 名字索引混入 Tda Spec 长度／静息高度 | NativeAOT |
| `candidate/csharp/src/GakuMotion.VmdImporter/Resources/mmd_source_skeleton.json` | 外部模型 92 骨矩阵、head/tail/length 导出结构 | NativeAOT |
| `tools/vmd2gkmotion/tooth_appearance_lut.json` | YYB/Saki 几何训练的观测与 RBF 模型 | NativeAOT |
| `tools/vmd2gkmotion/root_retarget_profiles.json` | 真实 VMD 与官方录制绑定的校准数据 | NativeAOT |

上表的判断是**分发依据尚未提供**，不等于认定侵权。作者通用算法、接口名字和运行时输入计算
与这些静态数据进行了区分，例如 `GkWaistSupport.hpp` 的解析公式、`GkFaceRuntime.hpp` 的字段协议仍保留。
没有把“实现兼容 MMDD 行为”推断为复制 GPL 实现；已明确的 NumPy 源适配按 BSD-3-Clause 保留归属。

原项目对上表文件有实际 include、类型使用或嵌入资源依赖，因此省略它们后不能声称完整构建成功。
本候选保留预检和精确源文件 SHA256；不伪造资源、不改写冻结求值器、不制造替代模型。
未来若取得依据，应对确切新字节重新审核并更新冻结清单，再完成对应构建和必要验证。

### 验证边界

`Contracts` 只运行合成的 stage-load detour 边界测试：使用伪宿主服务、合成字符串和内存对象。
它不加载游戏 DLL、资源、模型、动作或捕获数据。完整 Runtime／NativeAOT 在预检处被阻止。
产物与日志保存在源码仓之外。结果由上级任务的验证记录给出，本文件不把未运行项目写为通过。

### 2026-11-01 文档入口恢复

原冻结候选以本地审核说明替换了产品首页，且没有带入原仓用户文档。本次从
`2b28931e8b50b780668baec5e3982927667c3e08` 恢复安装、使用、排障、架构与格式入口；
仅对当前构建参数、失效链接和历史文档分区作适配。原实现与11项缺省输入保持不变。
新增或调整文档在清单中记录原路径与本次适配，不以工具生成的 approved 标记替代内容判断。

## 2026-11-04 交互功能与配套导入器

从活动源码固定 `000a9327417bd2145ac91a123566f0676ba7fd99` 选择品牌、多文件拖入、角色名单与实际脸型身份、四角色 Miku 嘴形拟合、腰部和物理默认值、相机追随与部位、全局倍速及每文件时序的生产闭包。配套身体导入 ABI 为 3、镜头 ABI 为 1。没有整仓覆盖；此前11个恢复输入逐字节保留，vendored、许可证、公开构建入口和既有产品文档继续保留。

四角色拟合新增两份便携参数 JSON、生成器和生成头；保留原始 portable JSON 的 CRLF Git 字节，与 e626b354 的来源相同。重新生成的 JSON 和 C++ 头逐字节匹配。参数来源记录不是普通构建条件。该功能默认开启、明确标为实验，适用范围与已知行为见 docs/USAGE.md；没有导入原模型或训练输入。

公开 GakuPlugin.h 和 GakuFileDrop.h 使用 GakuLoader ed5ad70819907c0a0f5acc25e804b0aea205fa57 的 canonical LF 版本，与宿主共用SDK。其余生产改动按上述固定源码的原始 Git blob 导出；具体来源、旧文件身份和本次字节身份均写入清单。

测试只使用自有合成夹具。聚焦核心入口从现有单测去除录制向量与真实曲目执行项；不把它称为原仓完整测试套件。新增 ABI 3 合成导入/reader 入口 tests/run_import_e2e.py。没有纳入 SP/牙齿研究工具、录制、素材、Control API/Clock/Timeline/Director 原型、工作区鞋底文案或临时目录。构建与测试回执在独立输出目录记录，不等同游戏加载或视觉验收。

## 2026-11-04 镜头选择与配套恢复

选择活动源码提交 `3a33452` 的镜头四项选择、Game 状态保存、旧 Hold 文件迁移及其合成/ImGui 交互测试。七个源文件按原始 Git blob 导出；`GakuMotion.hpp` 只在 staging 既有 GUI 消费分支添加 enabled 保存条件，未引入 Control API 重构。使用文档仅更新镜头段，保留 staging 路径与构建约定。

此次应与 VRify 的空共享目标修复一同进入候选，避免只改界面却保留导致接口未就绪的启动故障。用户已确认开发版这次镜头效果恢复；该确认不扩展为全部 VR、角色、场景或配置矩阵验收。staging 本轮只做离线构建和合成测试，未部署游戏。

未引入 Control API、Clock、Timeline、Director、DevAgents、真实输入或工作区其他改动。README 的既有未提交文案保留；冻结清单仍记录其提交版本，不能用当前工作树全文哈希冒充任务源码。具体范围与回执见 [镜头候选说明](docs/CAMERA_MODE_RECOVERY_20261104.md)。

## 2026-11-05 无角色时的曲库导入推进

从研发仓固定 `0870da032cdb25d02f2d3c7803fce4afc618c743` 只选择 `Core::ProcessPendingLibraryCommands`、`MotionTick` 的对应调用与合成测试闭包。文件导入、镜头导入和曲库刷新由宿主帧启动及轮询，摄影准备阶段不再依赖角色回调。处理仅取队首连续的三类文件命令；先前的播放、停止、选曲或场景控制保持原顺序及角色回调时机。

生产方法直接采用该提交的对应代码段，未以研发仓全文覆盖 staging。测试接入 staging 的 `test_refresh_core.cpp`，并在新测试头中加入两帧合成动作写入器，替代研发测试对另一夹具头的引用；不访问用户动作或其他真实素材。原生导入 ABI、算法参数、依赖及公开构建入口不变。没有引入 Control API、Studio、眼动研究或原工作区鞋底文案。

来源、适配和 staging 离线检查见 [导入修复说明](docs/ACTORLESS_IMPORT_FIX_20261105.md)；逐文件当前字节与历史身份继续由 `freeze-manifest.json` 记录。本轮不部署游戏，不新增 staging 进程加载或视觉验收声明。
