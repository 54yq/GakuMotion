import { ChevronDown } from 'lucide-react'
import { Button } from '@/components/ui/button'
import { Checkbox } from '@/components/ui/checkbox'
import { Collapsible, CollapsibleContent, CollapsibleTrigger } from '@/components/ui/collapsible'
import { Label } from '@/components/ui/label'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { Separator } from '@/components/ui/separator'
import { Slider } from '@/components/ui/slider'
import { motionStore, useMotionStore } from '../store'
import { CAMERA_FOLLOW_TARGETS, CAMERA_MODES, type CameraSettings, type CameraTransform } from '../protocol'
import { cameraControlModeIndex, setCameraControlMode, upgradePerModeTransforms } from '../cameraMode'
import { Vector3Field } from '../components/NumberField'
import { PendingBadge } from '../components/Unsupported'

function activeTransformKey(selection: number): keyof Pick<CameraSettings, 'transformTrack' | 'transformFollowMove' | 'transformLookAt'> {
  if (selection === 1) return 'transformTrack'
  if (selection === 2) return 'transformFollowMove'
  return 'transformLookAt'
}

export function CameraPage() {
  const camera = useMotionStore((state) => state.camera)
  const draft = useMotionStore((state) => state.cameraDraft)
  const pending = useMotionStore((state) => state.cameraPending)
  const supported = useMotionStore((state) => state.capabilities.includes('camera.set'))
  const saveSupported = useMotionStore((state) => state.capabilities.includes('camera.save'))

  const selection = cameraControlModeIndex(draft)
  const userMode = selection - 1
  const transformKey = activeTransformKey(selection)
  const activeTransform: CameraTransform = draft[transformKey]

  const patch = (next: Partial<CameraSettings>) => {
    if (!supported) return
    motionStore.patchCameraDraft(next)
  }

  const patchTransform = (next: CameraTransform) => {
    const upgraded = upgradePerModeTransforms(draft)
    const merged: CameraSettings = { ...upgraded, [transformKey]: next }
    // Keep legacy offsets mirrored, like the ImGui page does on commit.
    if (selection === 1) merged.vmdOffset = { ...next.position }
    else merged.followOffset = { ...next.position }
    patch(merged)
  }

  const resetTransform = () => {
    const defaults: Record<number, CameraTransform> = {
      1: { position: { x: 0, y: 0, z: 0 }, rotation: { x: 0, y: 0, z: 0 } },
      2: { position: { x: 0, y: 0.6, z: 3 }, rotation: { x: 0, y: 0, z: 0 } },
      3: { position: { x: 0, y: 0.6, z: 3 }, rotation: { x: 0, y: 0, z: 0 } },
    }
    patchTransform(defaults[selection] ?? defaults[1])
  }

  const disabled = !supported

  return (
    <div className="space-y-5" data-testid="camera-page">
      <section className="space-y-2">
        <h3 className="text-sm font-medium">镜头模式</h3>
        <div className="flex items-center gap-2">
          <Select
            value={String(selection)}
            disabled={disabled}
            onValueChange={(value) => {
              const next = setCameraControlMode(upgradePerModeTransforms(draft), Number(value))
              patch(next)
            }}
          >
            <SelectTrigger className="h-8 w-[220px]" aria-label="镜头模式">
              <SelectValue />
            </SelectTrigger>
            <SelectContent>
              {CAMERA_MODES.map((label, index) => (
                <SelectItem key={label} value={String(index)}>
                  {label}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
          {!supported && <PendingBadge />}
        </div>
        <p className="text-sm text-muted-foreground">
          镜头接管: {camera.owned ? '已生效' : '未接管'} | 运镜轨道: {camera.hasTrack ? '已加载' : '无'}
        </p>
        {!camera.available && <p className="text-sm text-warning">当前镜头接口未就绪。</p>}
        {camera.error && <p className="text-sm text-destructive">镜头轨道错误: {camera.error}</p>}
        {camera.followTargetError && <p className="text-sm text-destructive">跟随目标错误: {camera.followTargetError}</p>}
        {pending && <p className="text-sm text-muted-foreground">正在应用镜头设置…</p>}
        <p className="text-xs text-muted-foreground">选择“游戏”恢复游戏手动镜头，并保留 GakuMotion 镜头参数。</p>
        {userMode === 0 && !camera.hasTrack && (
          <p className="text-sm text-warning">当前曲目没有可用运镜轨道，VMD 模式将保持固定机位。</p>
        )}
      </section>

      {draft.enabled && (
        <>
          <Separator />
          <section className="space-y-3">
            <h3 className="text-sm font-medium">镜头位置与旋转偏移</h3>
            {userMode === 0 && (
              <div className="space-y-1.5">
                <div className="flex items-center gap-3">
                  <span className="w-24 shrink-0 text-sm text-muted-foreground">VMD 运镜范围</span>
                  <Slider
                    aria-label="VMD 运镜范围"
                    className="max-w-xs flex-1"
                    min={0.3}
                    max={1}
                    step={0.01}
                    disabled={disabled}
                    value={[draft.vmdRangeScale]}
                    onValueChange={([value]) => patch({ vmdRangeScale: value })}
                  />
                  <span className="text-sm tabular-nums text-muted-foreground">{draft.vmdRangeScale.toFixed(2)} 倍</span>
                </div>
                <p className="text-xs text-muted-foreground">
                  以角色附近为中心缩小轨迹；会改变构图，不能保证避免穿模。点击下方保存镜头设置可保留。
                </p>
              </div>
            )}
            <div className="space-y-1.5">
              <Label className="text-xs text-muted-foreground">位置 (米)</Label>
              <Vector3Field
                labelPrefix="镜头位置"
                value={activeTransform.position}
                min={-50}
                max={50}
                step={0.01}
                disabled={disabled}
                onCommit={(position) => patchTransform({ ...activeTransform, position })}
              />
            </div>
            <div className="space-y-1.5">
              <Label className="text-xs text-muted-foreground">旋转 (度)</Label>
              <Vector3Field
                labelPrefix="镜头旋转"
                value={activeTransform.rotation}
                min={-180}
                max={180}
                step={0.5}
                decimals={1}
                disabled={disabled}
                onCommit={(rotation) => patchTransform({ ...activeTransform, rotation })}
              />
            </div>
            <Button variant="outline" size="sm" disabled={disabled} onClick={resetTransform}>
              重置位置与旋转偏移
            </Button>
          </section>
        </>
      )}

      {draft.enabled && (userMode === 1 || userMode === 2 || draft.followActor) && (
        <>
          <Separator />
          <section className="space-y-3">
            {draft.mode === 2 && (
              <div className="flex items-center gap-2">
                <span className="text-sm text-muted-foreground">跟随部位</span>
                <Select
                  value={String(draft.followTarget)}
                  disabled={disabled}
                  onValueChange={(value) => patch({ followTarget: Number(value) })}
                >
                  <SelectTrigger className="h-8 w-[180px]" aria-label="跟随部位">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    {CAMERA_FOLLOW_TARGETS.map((label, index) => (
                      <SelectItem key={label} value={String(index)}>
                        {label}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>
            )}
            <h3 className="text-sm font-medium">追随速率 (Target Rate)</h3>
            <p className="text-xs text-muted-foreground">
              0 停止追随，0.1 平滑追随，1 即时追随。数值越小，追赶越慢。X / Y / Z 为接管时镜头的右 / 上 / 前方向。
            </p>
            <Vector3Field
              labelPrefix="追随速率"
              value={draft.followRate}
              min={0}
              max={1}
              step={0.01}
              disabled={disabled}
              onCommit={(followRate) => patch({ followRate })}
            />
            <Button variant="outline" size="sm" disabled={disabled} onClick={() => patch({ followRate: { x: 0.1, y: 0.1, z: 0.1 } })}>
              重置追随速率
            </Button>
          </section>
        </>
      )}

      <Separator />
      <Collapsible>
        <CollapsibleTrigger className="flex items-center gap-1.5 text-sm font-medium" disabled={!draft.enabled}>
          <ChevronDown className="size-4" />
          高级镜头设置
        </CollapsibleTrigger>
        <CollapsibleContent className="mt-3 space-y-4">
          {userMode === 0 && (
            <div className="flex items-center gap-2">
              <Checkbox
                id="camera-follow-actor"
                checked={draft.followActor}
                disabled={disabled}
                onCheckedChange={(checked) => patch({ followActor: checked === true })}
              />
              <Label htmlFor="camera-follow-actor">镜头随角色平移</Label>
            </div>
          )}
          {(userMode === 1 || userMode === 2 || draft.followActor) && (
            <div className="space-y-1.5">
              <h4 className="text-sm font-medium">位移比例（旧版跟随幅度）</h4>
              <p className="text-xs text-muted-foreground">
                缩放最终跟随的角色位移：0 不跟随该轴，0.5 跟一半，1 完整跟随。此项决定目标幅度，追随速率决定追赶快慢。
              </p>
              <Vector3Field
                labelPrefix="位移比例"
                value={draft.followFactor}
                min={0}
                max={1}
                step={0.01}
                disabled={disabled}
                onCommit={(followFactor) => patch({ followFactor })}
              />
              <Button variant="outline" size="sm" disabled={disabled} onClick={() => patch({ followFactor: { x: 1, y: 1, z: 1 } })}>
                恢复完整位移
              </Button>
            </div>
          )}
          {(userMode === 1 || userMode === 2) && (
            <div className="space-y-1.5">
              <h4 className="text-sm font-medium">注视目标偏移 (Target Offset，相对所选部位)</h4>
              <Vector3Field
                labelPrefix="注视偏移"
                value={draft.targetOffset}
                min={-5}
                max={5}
                step={0.01}
                disabled={disabled}
                onCommit={(targetOffset) => patch({ targetOffset })}
              />
              <div className="flex items-center gap-3">
                <span className="w-24 shrink-0 text-sm text-muted-foreground">镜头 FOV</span>
                <Slider
                  aria-label="镜头 FOV"
                  className="max-w-xs flex-1"
                  min={15}
                  max={90}
                  step={0.5}
                  disabled={disabled}
                  value={[draft.followFov]}
                  onValueChange={([value]) => patch({ followFov: value })}
                />
                <span className="text-sm tabular-nums text-muted-foreground">{draft.followFov.toFixed(1)}°</span>
              </div>
            </div>
          )}
          <div className="space-y-2">
            <div className="flex items-center gap-2">
              <Checkbox
                id="camera-limit-scene"
                checked={draft.limitToScene}
                disabled={disabled}
                onCheckedChange={(checked) => patch({ limitToScene: checked === true })}
              />
              <Label htmlFor="camera-limit-scene">限制镜头在场景范围内</Label>
            </div>
            {draft.limitToScene && (
              <div className="space-y-2">
                <Label className="text-xs text-muted-foreground">最小边界 (Bounds Min)</Label>
                <Vector3Field
                  labelPrefix="最小边界"
                  value={draft.boundsMin}
                  min={-50}
                  max={50}
                  step={0.1}
                  decimals={1}
                  disabled={disabled}
                  onCommit={(boundsMin) => patch({ boundsMin })}
                />
                <Label className="text-xs text-muted-foreground">最大边界 (Bounds Max)</Label>
                <Vector3Field
                  labelPrefix="最大边界"
                  value={draft.boundsMax}
                  min={-50}
                  max={50}
                  step={0.1}
                  decimals={1}
                  disabled={disabled}
                  onCommit={(boundsMax) => patch({ boundsMax })}
                />
              </div>
            )}
          </div>
        </CollapsibleContent>
      </Collapsible>

      <Separator />
      <div className="space-y-2">
        <Button variant="outline" size="sm" disabled={!saveSupported} onClick={() => void motionStore.saveCamera()}>
          保存镜头设置
        </Button>
        <p className="text-xs text-muted-foreground">修改立即生效；保存后下次启动自动载入，作用于全部曲目。</p>
        {camera.notice && (
          <p className={`text-sm ${/失败|无效/.test(camera.notice) ? 'text-destructive' : 'text-muted-foreground'}`}>
            {camera.notice}
          </p>
        )}
      </div>
    </div>
  )
}
