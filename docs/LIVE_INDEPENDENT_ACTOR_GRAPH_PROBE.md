# 原主角独立动画图调查

GakuActors 的新增人物流程是 `CampusActorManager.CreateActor` → 新控制器的 `BuildAsync`。原生构建同时初始化面部、表情和角色动画。原主角独立图实验需要复用其中的建图和骨架绑定能力。

在当前客户端元数据中已找到三个入口。

- `CampusActorController.GetCustomGraphAnimationPlayer` 和对应的 `_customGraphAnimationPlayer`。需要确认该播放器何时创建、是否使用当前 Animator、是否已有原生物理作业。
- `ActorAnimationSimpleInitializerUtility.Setup(animator, rootTransform, graph, rig, builder)` 和 `Clear(builder, graph, rig)`。参数可指向独立图，但现有数据还无法确认其初始化和清理是否修改原角色所持有的 rig。
- `CampusActorAnimationBuilder.Build(animator, graph, actorAnimationRig)` 与 `BuildRigPlayable(graph)`。需要确认它们如何把当前服装的骨架数据交给 `CampusActorAnimationJob`，尤其是两个图之间是否共享会被销毁的原生数据。已确认原播放器自己的 `CreatePlayableGraph` 会先销毁现有图。

## v4.1.1 诊断构建

本构建基于 v4.1。在已确认角色图的普通 Live 和首次 Motion 接管阶段，读取原主角及首个副角色各一次。输出 `IndependentActorGraph` 记录。如果已有自定义图，核对句柄是否与原图不同、首个动画输出是否绑定当前 Animator，并识别该图中实际可读取的 `CampusActorAnimationJob` 和动画片段数量。`scan=0` 表示读取未完成。

同一进程已有的 `live_code_<pid>/methods.tsv` 采集覆盖自定义播放器、建图器、初始化工具和骨架类。仅从现有对象读取字段和图的元数据。游戏的自定义播放器创建、建图、绑定及清理方法不会被这个诊断入口调用。

## 功能版的验证条件

现有证据只确认候选入口。本地功能版需要验证新图确实属于同一个 Animator，图中存在原主角服装的 Swing 作业，原图的句柄和暂停状态可恢复。试验应在主角原衣服下覆盖带眨眼轨和无眼部轨的 Motion，记录播放、暂停、拖动和退出时的表情驱动、原生求解写入与原图恢复结果。
