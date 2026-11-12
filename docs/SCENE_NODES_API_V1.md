# Scene Nodes API v1

`gakumotion.scene-nodes/v1` 是 GakuLoader 进程内服务，公开头为
[GakuMotionSceneNodes.h](../include/GakuMotionSceneNodes.h)。既有
`gakumotion.control/v0` 保持原布局与语义，继续提供统一表演时间和播放状态。

独立 Trailer 插件负责 FPS、起止帧、隐藏/显示顺序和状态求值。本服务只负责当前场景中受限背景节点的
枚举、身份校验、完整可见性状态写入，以及恢复接管前的真实 `activeSelf`。

## 固定作用域

枚举沿用 StageManager 的探测列表顺序，只返回短名精确为 `cmn-parts`（忽略 ASCII 大小写）的节点的
一级子项。排除短名含 `floor`（忽略大小写）、cmn-parts 容器本身、角色、系统、灯光、碰撞及边界节点。
不开放任意 Unity 路径、指针、反射、场景加载或对象查找。原探测范围的深度及每层数量上限仍有效；
数量是当前实际探测并通过过滤的条目数，不是资产包内全部潜在物件数。

floor 节点与容器本身不会被该接口写入。若容器或其祖先本来隐藏，接口不会强行打开它来使子项可见；
当前状态不可用于可靠控制时返回 `NotReady`。节点 `activeSelf` 不等同最终画面可见性。

## 调用方式

通过宿主 `queryService` 查询服务名、版本 `1` 和精确 `sizeof(Api)`；校验返回表的版本、大小及函数指针。
查询可在任意线程执行，只复制同步快照，绝不调用 Unity。所有变更必须来自 Unity 主线程的
`onTick/onActorTick`；UI 线程只修改插件自己的待处理设置。

1. `getState` 读取就绪状态、`catalogRevision` 与节点数量。
2. 按 `getNode(revision,index)` 取得不透明 handle、短名、探测序号与当前 `activeSelf`。
3. `acquire(revision)` 完整读取当前真实状态，建立独占 lease；失败时 lease 为零且不写节点。
4. `apply(lease,revision,values)` 提交该目录的全部 handle，每项恰好一次，值只能是零或一。
5. 停用编排时 `release(lease)` 恢复基线；失败须继续重试，不能当作已经恢复。

目录身份、层级或顺序变化后 revision 改变；旧 handle/lease 返回 `StaleCatalog`。节点可见性变化本身
不会改 revision。不得把 handle 当成对象地址，也不能在新场景重复使用旧目录索引。

`getState` 返回 `Ok` 仅表示快照已复制；实际就绪/失败原因在 `State.ready/status/message` 中。
不能将“成功复制状态”误认为“场景已就绪”。`owned` 表示持有独占恢复责任，不等同一次 apply 成功。
`ready` 与 `Node.active` 是缓存提示；acquire 才用新鲜 Unity 读取校正基线，每次变更也重新校验。
正常目录同步不枚举 Unity 全场景，插件应按完整目标状态是否改变合并重复 apply，尤其是暂停期间。

## 写入、失败与恢复

先验证完整批次、所有对象身份、父容器和全部值，再执行 Unity setter 并读取实际状态确认。
格式错误、重复/缺失/未知 handle、旧 revision 不会先写一部分合法条目。

若同一场景只有部分身份失效，apply 可以返回 `StaleCatalog` 并继续保留 lease/恢复责任；release 只恢复
仍匹配的对象，绝不写入复用地址的新对象。因此调用方即使收到 `StaleCatalog`，也必须先 release 旧 lease，
待恢复成功或确认该令牌整体失效后再丢弃。整个场景换代则不向旧对象写入。

Unity setter 或 readback 本身失败时，游戏可能已经修改了部分对象。此时返回 `Failed`，保留完整基线并
标记 `restorePending`，拒绝继续 apply；调用方必须 release 重试恢复。服务不谎称跨 Unity setter 原子事务。

正常 StageManager 背景刷新不覆盖 lease 拥有的子项；同一范围的手动写入不能与插件并行争用。
Stop、现有暂停保护/撤销与关闭注入路径依然调用统一恢复，旧 lease 随之失效。
对象已卸载或场景身份变化时丢弃旧引用，不把旧基线写入新对象；新目录重新捕获后才能接管。

该服务不修改现有截图/分享保护，不绕过准入条件，不读取 Windows 音频时钟，也不保存插件编排配置。
目录查询、离线测试与 DLL 文件部署不能替代真实录制场景的效果验收。
