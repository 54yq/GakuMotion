# 原生表情尾部（历史离线工具契约）

本文保留原研发工具的契约与历史应用记录；相关 Python 工具未随本源码仓提供。日常导入使用游戏内面板，见[使用指南](../USAGE.md)。下方命令是历史工具用法，不是当前仓库的安装步骤。

`.gkclip` v4/v5 将 `VLActorFaceModel` 命名通道和逐帧标量保存在文件尾部。
使用已有原生表情源替换这些通道时，可以保留身体、根位移和内嵌相机的原始字节。
本文说明离线工具的契约；游戏界面中的常规 VMD 导入见 [使用指南](../USAGE.md)。

工具：`native_facial.py`（库）、
`refresh_native_facial.py`（单文件 CLI）、
`apply_native_facial.py`（manifest 部署器）。

## 1. 为什么只换尾部

v4/v5 的表情尾部是文件最后一节，布局见 [动作与镜头格式](ACTION_FORMATS.md)：

```
[... 骨表 ...][... 帧数据 ...][u8 has_camera][camera][u32 n][名称表][f32 weights]
```

前四段与表情无关，且是几何/IK/相机工作的全部产物。尾部起点可由
`4 + Σ(2+utf8_len) + 4*total_frames*channel_count` 从旧尾部反推，所以

```
candidate = source_bytes[:tail_offset] + encode_tail(new_names, new_weights)
```

在字节级成立。它确认身体、根位移与内嵌相机数据没有改变，不替代新表情的实机判断。
工具会断言 `root_positions`、`bone_deltas`、`camera_frames`、
骨表、fps、帧数逐一相等，并把前缀 SHA-256 写进报告。

不要用 `GkClipData.serialize()` 重建整条 clip：它会对四元数重新归一化，往返结果
虽然数值等价，但不再逐字节相同，判据就失效了。

## 2. 数据源

| kind | 输入 | 覆盖 | 用途 |
|---|---|---|---|
| `asset` | Unity `_f` AnimationClip（`tree.json`）+ MotionDefine 绑定表 + face model 槽名表 | 由构造决定，通常全长 | **首选**，可复现，无录制丢帧 |
| `recorder` | GakuRecorder compact NPZ（`time` / `face_weights_raw` / `motion_channel_names`） | 实录区间 | 交叉验证 asset 解码是否正确 |

`asset` 解码走 Unity 的 streamed-scalar 三次多项式：多段 key 帧按
`((a·dt+b)·dt+c)·dt+d` 求值，超出 `m_StopTime` 后取最后一段（Unity 的 clamp 语义），
`m_ConstantClip` 的常数轨按全帧填充。槽位由属性名 `blendShapeWeights.wX.wY` 反解
为 `X*16+Y`，不依赖任何硬编码表意。

`recorder` 需要一条已拟合的身体时钟把录制时间映射到 clip 时间
（`clip_t = (t - offset) / rate`）。这个时钟来自身体骨的刚体配准，不是猜的：
BBP 的拟合结果是 `rate 0.99992105`、`offset −1.1866610 s`、held-out 骨 p95 ≤ 1.42°，
次优 offset 的 RMS 为 28.77°，无歧义。

## 3. 覆盖是硬门禁

`build_tail` 默认要求源覆盖 clip 的每一帧。缺口不会被静默插值或保持末值：

- 差集非空且 `require_full=True` → 抛错，并报出缺帧数与源区间；
- 显式 `--allow-partial` → 缺口帧权重写 **0**（中性脸），并在报告里 `coverage.partial=true`
  记录头/尾缺口帧数。

BBP 的实测差距值得记住：实录只覆盖 1.189–147.110 s 的 8755/9443 帧（92.7%），
尾部 616 帧缺失，而那段里 `mouth_026` 等通道仍有内容，不能简单保持。所以
「全长」只能由 `_f` 资产提供，实录只作验证（共同区间 MAE 0.031 pp）。

## 4. 值域与通道契约

- 原生存 **0..100**（`raw_VLActorFaceModel`）；磁盘存 **0..1**。转换是显式 `/100`，
  报告记 `weight_domain.scale=100`。这与运行时 `native_weight = 100 * clip_weight`
  互为逆运算，重复缩放不会发生（`GkMotionData` 会对越界值直接判文件非法）。
- 只导出 **bound** 槽：108 个槽里没有被任何绑定覆盖的列保持 NaN、不写入磁盘。
  BBP 官方资产绑定 84 个槽（42 条 streamed + 50 条 constant），因此尾部是 84 通道。
- 通道名必须非空且唯一、数量 ≤ 1024；顺序取 face model 槽序，保证同源重跑字节一致。
- 不裁剪恒零通道。省下的体积不值得引入「缺通道 = 0 还是未知」这层额外语义。

## 5. 眼动与版本边界

`b_eye.*` 是混合形状，属于本流程替换的尾部。HID 21/22 的眼球旋转属于骨骼轨道，
不由尾部替换工具创建或修改。工具接受 v4/v5；已有 v5 眼动骨保留在前缀中。

若要向原本没有眼动的 clip 添加 Head 空间眼动增量，需重新编码为 v5 并更新骨表，
不能仅替换表情尾部。历史 BBP 样本的眼骨峰值为 4.08°、均值 1.30°，80.3% 帧大于
0.5°；当时的 52 骨 v4 帧跨度为 844 B，增加两根眼骨后为 876 B。该测量只描述该样本。

## 6. 历史用法（工具未随本仓提供）

从仓库根目录查看工具参数：

```powershell
python tools/refresh_native_facial.py --help
python tools/apply_native_facial.py --help
```

单文件工具的 `clip` 指向源动作，`--out` 必须指向另一条路径。使用 `--source asset`
时提供 `--tree`、`--motion-define`、`--face-model`；使用 `--source recorder` 时提供
`--npz` 和从当前录制拟合的 `--clock-rate`、`--clock-offset`。不要把 BBP 的历史时钟
复制到另一首动作。可用 `--expected-fps` 检查动作采样率；`--allow-partial` 显式允许缺口。

输出旁会写 `<name>.native_facial_report.json`：前缀长度与哈希、源文件哈希、
覆盖统计、新增/移除/变更通道（共有通道的 MAE 与 max，单位 pp）。

## 7. 历史部署与回滚

`tools/apply_native_facial.py` 读 `patches/*.json`：

manifest 需要可访问的原生表情源和目标曲目。先使用 `--dry-run` 生成与校验候选，
确认报告后再应用；`--verify` 只读核对已安装状态。仓库中的 manifest 包含历史本地
资源位置，不能假定在另一台机器可直接执行。

- **幂等**：候选 SHA 与现 clip 相同 → `already_applied`，不落盘、不建备份。
- **可逆**：原 clip 备份在 `<entry>/_native_facial_backup/motion.gkclip.<sha16>`，
  报告 `install.previous_sha256` 记录血缘。回滚 = 拷回备份。
- **血缘**：旧 `motion.morph_report.json` 不重写（它记录 VMD 派生尾部是怎么来的），
  新报告的 `lineage` 段显式指认它、它的 `clip_sha256` 与 `channel_count`。否则审计时
  会看到同一目录两份报告、两个哈希、没有关系说明。
- **原子**：候选先写到 entry 内的临时名，校验通过后 `os.replace`；校验不过则原文件不动。
- `--verify` 只读，检查 report 的 `output_sha256` 与当前文件一致、前缀哈希未漂移。

## 8. BBP 历史应用记录

以下是 `patches/offline_native_facial.json` 对 *Boom Boom Pow (Offline Official)*
的历史补丁记录，不代表当前游戏目录的安装状态：

| 项 | 补丁前 | 补丁后 |
|---|---|---|
| 文件大小 | 8,613,311 B | 11,145,220 B |
| 通道数 | 17（VMD morph 派生） | 84（官方 native bound） |
| clip SHA-256 | `0def03ee…` | `4aa71633f328ab70…` |
| 前缀 | — | 7,970,892 B，SHA `a29e45a2…` 不变 |

通道层面：17 个共有通道全部重写（`b_eye.eye_002` MAE 19.45 pp、`b_brow.brow_003`
10.91 pp、`b_brow.brow_001` 9.28 pp 差异最大），新增 67 路（含 `mouth_026` 牙齿、
`b_iris.*`），移除 0 路。

在该样本中，旧 clip 的 `mouth_026` 有效帧为 0，官方轨里是 666 帧
（7.05%）、峰值 100，与 `mouth_025` 仅 83% 帧相等、r=0.567。二者在该样本中是独立曲线；
这不能推广为所有角色和口型的通用牙齿语义。

`motion.morph_report.json` 保持不动。它记录的是补丁前 VMD 派生尾部如何生成，
其 `clip_sha256` 指向那个旧文件、已不代表当前 clip；新报告的 `lineage` 段显式
指认这层取代关系，而不是去改写历史报告。新事实写在
`motion.native_facial_report.json`。`GkRuntimeConfig` 见到该目录存在 morph 报告
或尾部 count>0 就会置 `hasFacial`，无需改 `info.json`。
