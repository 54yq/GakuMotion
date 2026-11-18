# 镜头选择与启动恢复候选

本轮选择研发仓提交 `3a33452` 的镜头修复进入 staging：镜头页和播放器统一「游戏 / VMD / 跟随 / 注视」，游戏选项释放接管并保留参数，移除独立接管开关和固定入口。旧 Hold 配置载入时迁移为 VMD，内部数值不重排。选择 Game 的 enabled 变化也纳入保存，两个入口共享待确认状态，避免旧快照使选择回弹。

`GakuMotion.hpp` 保留 staging 原来的 GUI 消费逻辑，仅添加 enabled 变化检测；不引入研发仓最近的 Control API helper、Clock 或 Timeline。其他七个源文件来自该提交的原始 Git blob。README 既有未提交改动不属于本次候选。

接口未就绪的故障还涉及 VRify 的共享目标判断；必须与对应 VRify 修复配套带入，不能把四项下拉本身视为启动故障修复。用户已确认研发部署版的本次镜头恢复；不代表全部 VR 或场景矩阵通过。

staging 本轮使用正常 Runtime 构建、851 项合成核心、80 项舞台合同及实际 ImGui 交互验证，不访问真实动作、模型或游戏。构建/测试输出位于工作区 `build/staging_camera_20261104/Motion/`，源码来源与文件身份写入 `freeze-manifest.json`。staging 重新编译产物的字节身份不同于已部署研发版，不声称已获得独立游戏内验收。
