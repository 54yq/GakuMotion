# 原主角独立动画图调查

GakuActors 的新增人物流程是 `CampusActorManager.CreateActor` → 新控制器的 `BuildAsync`。原生构建同时初始化面部、表情和角色动画。原主角独立图实验需要复用其中的建图和骨架绑定能力。

在当前客户端元数据中已找到三个入口。

- `CampusActorController.GetCustomGraphAnimationPlayer` 和对应的 `_customGraphAnimationPlayer`。需要确认该播放器何时创建、是否使用当前 Animator、是否已有原生物理作业。
- `ActorAnimationSimpleInitializerUtility.Setup(animator, rootTransform, graph, rig, builder)` 和 `Clear(builder, graph, rig)`。参数可指向独立图，但现有数据还无法确认其初始化和清理是否修改原角色所持有的 rig。
- `CampusActorAnimationBuilder.Build(animator, graph, actorAnimationRig)` 与 `BuildRigPlayable(graph)`。需要确认它们如何把当前服装的骨架数据交给 `CampusActorAnimationJob`，尤其是两个图之间是否共享会被销毁的原生数据。已确认原播放器自己的 `CreatePlayableGraph` 会先销毁现有图。

## v4.1.1 诊断构建

本构建基于 v4.1。在已确认角色图的普通 Live 和首次 Motion 接管阶段，读取原主角及首个副角色各一次。输出 `IndependentActorGraph` 记录。如果已有自定义图，核对句柄是否与原图不同、首个动画输出是否绑定当前 Animator，并识别该图中实际可读取的 `CampusActorAnimationJob` 和动画片段数量。`scan=0` 表示读取未完成。

同一进程已有的 `live_code_<pid>/methods.tsv` 采集覆盖自定义播放器、建图器、初始化工具和骨架类。仅从现有对象读取字段和图的元数据。游戏的自定义播放器创建、建图、绑定及清理方法不会被这个诊断入口调用。

## v4.2.1 实验构建

v4.1.1 的原主角实测在接管前后都读到 1 个原生 Swing 作业和已初始化的骨架，自定义图字段为空。原生建图器先查询该骨架的初始化状态，再从骨架读取原生作业。

v4.2.1 在 v4.2 上合入了 v4.1.2 的独立角色图实验。接管时创建新的 Unity 图和游戏原生建图器，使用原主角 Animator 与原有服装骨架。仅在确认原图暂停、全部新图输出归属同一 Animator、原生作业已连接且新图无原 Live 动画片段后播放。Motion 继续使用原有表情写入路径。求解验证有两秒窗口，须出现同一主角匹配且有骨骼写入的原生求解。退出先停止并释放实验图，保留游戏拥有的骨架和原图。

试验需要用原主角和原衣服覆盖眨眼或其他表情、头发与衣服、暂停、拖动和退出恢复。可读运行记录为 `IndependentActorGraph` 的启用、原生求解确认或回退，以及 `ActorClockMVP` 的恢复结果。
