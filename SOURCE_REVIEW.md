# 选取与缺项记录

冻结源提交：`2b28931e8b50b780668baec5e3982927667c3e08`。
本次只整理源码，不改原仓，不改算法，不部署，不操作真实素材。

## 已纳入

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

## 未纳入且阻止完整构建的必需输入

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

## 验证边界

`Contracts` 只运行合成的 stage-load detour 边界测试：使用伪宿主服务、合成字符串和内存对象。
它不加载游戏 DLL、资源、模型、动作或捕获数据。完整 Runtime／NativeAOT 在预检处被阻止。
产物与日志保存在源码仓之外。结果由上级任务的验证记录给出，本文件不把未运行项目写为通过。
