# 动作与镜头格式

本文描述当前 GakuMotion 读写器使用的磁盘契约。实现入口是 [`GkMotionData.hpp`](../../src/GkMotionData.hpp)、[`GkClipEncoder.cs`](../../candidate/csharp/src/GakuMotion.VmdImporter/Output/GkClipEncoder.cs) 和 [`GkCamera.hpp`](../../src/GkCamera.hpp)。本文以当前读写器为准；早期格式草案未随本源码仓附带。

## 格式与用途

| 文件 | 标识 | 姿态语义 |
| --- | --- | --- |
| `.gkclip` | `GKCLIPCL`，兼容 `GKCLIP\0\0` | 目标骨架父链上的局部旋转；主分发格式 |
| `.gk_world_dump` | `GKWDMPWD`，兼容 `GKWDMP\0\0` | 录制的 actor 相对世界旋转 |
| `.gkmotion` | `GKMO` | 旧世界姿态格式，保留读取兼容 |
| `.gkcamera` | `GKCAM\0\0\0` | 独立镜头轨道，含位置、旋转、投影和切镜标志 |

所有数值使用 little-endian。浮点字段为 IEEE 754 `float32`，四元数顺序为 `(x, y, z, w)`。文件扩展名帮助组织资源，实际动作格式由 magic 决定。

## 动作文件头与骨表

`.gkclip` 与 `.gk_world_dump` 使用 8 字节 magic，随后是 24 字节头部；旧 `GKMO` 的 magic 为 4 字节，同一组字段从偏移 4 开始。

| `.gkclip` 偏移 | 类型 | 字段 |
| --- | --- | --- |
| 0 | `char[8]` | magic |
| 8 | `u32` | version |
| 12 | `f32` | fps |
| 16 | `u32` | total_frames |
| 20 | `f32` | duration_seconds |
| 24 | `u32` | bone_count |
| 28 | `f32` | root_scale |
| 32 | 骨表 | `bone_count` 个变长条目 |

每个骨表条目是 `u16 humanoid_id`、`u8 name_length`、`u8 flags`，随后是相应字节数的 UTF-8 名称。bit 0 表示启用该骨旋转；v5 的 bit 1 表示 Head 空间眼动增量，只允许 HID 21/22，且须同时设置 bit 0。其余位不得设置。

骨骼 ID 和名称各自必须唯一。`.gkclip` 必须按父骨在前的拓扑顺序排列，不能把 humanoid ID 数值顺序当作骨表顺序。身体 profile 通常为 52 骨；含眼动的 profile 增加 HID 21/22。HID 54 是 UpperChest；实际通道数以骨表为准。

读取器建立两套父链：`bone_parents[i]` 是骨表数组下标，`bone_parent_hids[i]` 是 humanoid ID。按骨表排布的数组使用前者，按 HID 查表使用后者，二者不能互换。

## 帧数据与版本

骨表后逐帧交错存储：

```text
for each frame:
    f32 root_position[3]
    f32 rotation[bone_count][4]
```

帧跨度为 `12 + 16 * bone_count` 字节，始终包含根位移。位移已经换算成米，`root_scale` 记录转换系数，播放端不能再乘一次。`.gkclip` 的根位移描述动作轨迹；场景中的起始位置由运行时处理。

| `.gkclip` 版本 | 当前兼容语义 |
| --- | --- |
| v1 | 早期局部姿态；可启用仅作用于腿链的 `legRestBasis` 兼容补偿 |
| v2、v3 | 目标骨架局部姿态；v3 更新转换坐标约定，未改变帧布局 |
| v4 | 在相机标志与可选相机帧后增加表情通道表和权重 |
| v5 | 延续 v4 尾部，允许 Head 空间眼动增量标志 |

普通 `.gkclip` 骨旋转通过局部旋转 setter 写入。v2 及以上不预乘 `restLocal`；v1 在 `legRestBasis` 开启、绑定局部旋转偏离 Identity 大于 5° 时，才对 HID 1–6、19、20 使用 `restLocal * clipRotation`。该兼容条件以当前运行时代码为准，不能从早期草案的通用公式推导。

v5 眼动增量需通过目标角色眼睛的中性坐标系转换；辅助骨、角色适配和显式覆盖是独立运行时处理。

局部格式能够被不同角色读取，但格式本身不能证明动作与目标几何无关。显式目标腿几何烘焙可能产生依赖指定骨段长度的姿态，应结合 sidecar 判读，也不能据此认定鞋底接触通过。

## 动作尾部

帧数据之后是 `u8 has_camera`，值为 0 或 1。若为 1，紧随 `total_frames` 组 7 个 `f32`：镜头位置 XYZ、注视目标 XYZ、FOV。v4/v5 必须写出相机标志，即使没有内嵌镜头。

v4/v5 随后必须有表情尾部：

```text
u32 channel_count
for each channel:
    u16 utf8_name_length
    char[utf8_name_length] name
f32 weights[total_frames][channel_count]
```

通道为 `VLActorFaceModel` 命名槽，不是网格索引；权重必须有限且在 `[0,1]`。名称非空且唯一，通道数不超过 1024。无表情也要写 `channel_count=0`。尾部之外的额外数据会被运行时拒绝。字节保留式替换说明见 [原生表情尾部](NATIVE_FACIAL_TAIL.md)。

## 独立镜头轨道

`.gkcamera` v1 不要求重编身体动作。20 字节头部后是固定 40 字节采样：

| 字段 | 类型 |
| --- | --- |
| magic | `char[8]`，`GKCAM\0\0\0` |
| version | `u32`，1 |
| fps | `f32` |
| count | `u32` |
| 每帧 position | `f32[3]` |
| 每帧 rotation | `f32[4]`，XYZW |
| 每帧 fov | `f32` |
| 每帧 orthographic_size | `f32` |
| 每帧 flags | `u32` |

flags bit 0 为正交投影，bit 1 表示该采样与前一采样之间切镜。文件长度严格等于 `20 + 40 * count`。fps 必须为 `(0,240]`，帧数为 1 至 1,000,000，FOV 为 `[1,179]`，正交尺寸为正数；旋转须非零且有限，加载后归一化。

## 校验与证据范围

动作读取器接受 `.gkclip` v1–v5；其他动作格式接受 v1–v4。它检查头部、骨表、拓扑、有限值、单位四元数、相机及表情尾部，并应用默认 256 MiB 分配预算。文件合法不等于已部署、已加载或视觉正确。

维护时同时核对编码器、读取器和测试。格式顺序及防护由核心测试覆盖，执行入口见 [构建与验证](../../BUILD_NOTES.md)。历史测试数字和单曲视觉结论不构成文件格式的通用保证。
