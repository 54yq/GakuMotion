import * as React from 'react'
import { Button } from '@/components/ui/button'
import { Checkbox } from '@/components/ui/checkbox'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { ALL_BONES, BONE_GROUPS, type BoneOverrideEntry } from '../protocol'
import { motionStore, useMotionStore } from '../store'
import { PendingBadge } from './Unsupported'

const ANGLES: Array<{ key: 'x' | 'y' | 'z'; label: string }> = [
  { key: 'x', label: 'Pitch' },
  { key: 'y', label: 'Yaw' },
  { key: 'z', label: 'Roll' },
]

export function BoneSection() {
  const bone = useMotionStore((state) => state.bone)
  const selectedHid = useMotionStore((state) => state.selectedBoneHid)
  const symmetrical = useMotionStore((state) => state.boneSymmetrical)
  const capabilities = useMotionStore((state) => state.capabilities)
  const supports = (method: string) => capabilities.includes(method)
  const selectedDef = React.useMemo(
    () => ALL_BONES.find((entry) => entry.hid === selectedHid) ?? null,
    [selectedHid],
  )
  const activeOverride = React.useMemo(
    () => bone.overrides.find((entry) => entry.humanoidId === selectedHid) ?? null,
    [bone.overrides, selectedHid],
  )
  const selected: BoneOverrideEntry | null = React.useMemo(() => {
    if (activeOverride) return activeOverride
    if (!selectedDef) return null
    return {
      humanoidId: selectedDef.hid,
      boneName: selectedDef.name,
      enabled: true,
      spaceWorld: false,
      eulerDeg: { x: 0, y: 0, z: 0 },
      isDynamic: false,
    }
  }, [activeOverride, selectedDef])
  const hasOverride = (hid: number) => bone.overrides.some((entry) => entry.humanoidId === hid)

  return (
    <div className="space-y-3" data-testid="bone-section">
      <div className="flex flex-wrap items-center gap-2">
        <Button
          variant="outline"
          size="sm"
          disabled={!supports('bone.setMaster')}
          onClick={() => void motionStore.setBoneMaster(!bone.masterEnabled)}
        >
          {bone.masterEnabled ? '关闭覆盖总开关' : '开启覆盖总开关'}
        </Button>
        <Button variant="outline" size="sm" disabled={!supports('bone.reload')} onClick={() => void motionStore.reloadBoneOverrides()}>
          重新载入
        </Button>
        <Button variant="outline" size="sm" disabled={!supports('bone.saveToMotion')} onClick={() => void motionStore.saveBoneOverridesToMotion()}>
          保存至当前曲目
        </Button>
        <Button variant="outline" size="sm" disabled={!supports('bone.saveToGlobal')} onClick={() => void motionStore.saveBoneOverridesToGlobal()}>
          保存至全局
        </Button>
        <Button variant="outline" size="sm" disabled={!supports('bone.resetAll')} onClick={() => void motionStore.resetAllBoneOverrides()}>
          全部重置
        </Button>
        {!supports('bone.set') && <PendingBadge />}
      </div>
      {bone.notice && <p className="text-sm text-muted-foreground">{bone.notice}</p>}

      <div className="grid gap-3 lg:grid-cols-2">
        <div className="max-h-80 space-y-2 overflow-y-auto rounded-md border p-2">
          {BONE_GROUPS.map((group) => (
            <div key={group.title}>
              <p className="mb-1 text-xs font-semibold text-muted-foreground">{group.title}</p>
              <div className="flex flex-wrap gap-1">
                {group.bones.map((boneDef) => (
                  <button
                    key={boneDef.hid}
                    type="button"
                    onClick={() => motionStore.selectBone(boneDef.hid)}
                    className={`rounded px-2 py-1 text-xs transition-colors ${
                      selectedHid === boneDef.hid ? 'bg-primary text-primary-foreground' :
                      hasOverride(boneDef.hid) ? 'bg-primary/15 text-primary' : 'hover:bg-muted'
                    }`}
                  >
                    {boneDef.labelZh}{hasOverride(boneDef.hid) ? ' *' : ''}
                  </button>
                ))}
              </div>
            </div>
          ))}
        </div>

        <div className="space-y-3 rounded-md border p-3" data-testid="bone-editor">
          <p className="text-sm font-medium">
            {selected
              ? `${selectedDef?.labelZh ?? selected.boneName} (hid ${selected.humanoidId})${activeOverride ? '' : ' [未覆盖]'}`
              : '选择左侧骨骼以编辑'}
          </p>
          {selected && (
            <>
              <label className="flex items-center gap-2 text-sm">
                <Checkbox
                  checked={symmetrical}
                  onCheckedChange={(checked) => motionStore.setBoneSymmetrical(checked === true)}
                />
                对称联动
              </label>
              <label className="flex items-center gap-2 text-sm">
                <Checkbox
                  checked={selected.spaceWorld}
                  onCheckedChange={(checked) =>
                    void motionStore.setBoneOverride(
                      selected.humanoidId,
                      selected.eulerDeg.x,
                      selected.eulerDeg.y,
                      selected.eulerDeg.z,
                      checked === true,
                      selected.enabled,
                    )
                  }
                />
                世界坐标（否则局部坐标）
              </label>
              {ANGLES.map((angle) => (
                <div key={angle.key} className="flex items-center gap-2">
                  <Label className="w-12 text-xs">{angle.label}</Label>
                  <Input
                    className="h-8"
                    type="number"
                    min={-180}
                    max={180}
                    step={0.1}
                    value={selected.eulerDeg[angle.key]}
                    onChange={(event) => {
                      const next = { ...selected.eulerDeg, [angle.key]: Number(event.target.value) }
                      void motionStore.setBoneOverride(
                        selected.humanoidId,
                        next.x,
                        next.y,
                        next.z,
                        selected.spaceWorld,
                        selected.enabled,
                      )
                    }}
                  />
                  <Button
                    variant="ghost"
                    size="sm"
                    onClick={() => {
                      const next = { ...selected.eulerDeg, [angle.key]: selected.eulerDeg[angle.key] + 5 }
                      void motionStore.setBoneOverride(
                        selected.humanoidId,
                        next.x,
                        next.y,
                        next.z,
                        selected.spaceWorld,
                        selected.enabled,
                      )
                    }}
                  >
                    +5°
                  </Button>
                  <Button
                    variant="ghost"
                    size="sm"
                    onClick={() => {
                      const next = { ...selected.eulerDeg, [angle.key]: selected.eulerDeg[angle.key] - 5 }
                      void motionStore.setBoneOverride(
                        selected.humanoidId,
                        next.x,
                        next.y,
                        next.z,
                        selected.spaceWorld,
                        selected.enabled,
                      )
                    }}
                  >
                    -5°
                  </Button>
                </div>
              ))}
              <div className="flex gap-2">
                <Button
                  variant="outline"
                  size="sm"
                  onClick={() =>
                    void motionStore.setBoneOverride(
                      selected.humanoidId,
                      0,
                      0,
                      0,
                      selected.spaceWorld,
                      selected.enabled,
                    )
                  }
                >
                  全部归零
                </Button>
                <Button variant="outline" size="sm" onClick={() => void motionStore.resetBoneOverride(selected.humanoidId)}>
                  恢复默认值
                </Button>
              </div>
            </>
          )}
        </div>
      </div>

      {bone.overrides.length > 0 && (
        <div className="rounded-md border p-2">
          <p className="mb-1 text-xs font-semibold text-muted-foreground">活跃覆盖（{bone.overrides.length}）</p>
          <div className="flex flex-wrap gap-1">
            {bone.overrides.map((entry) => (
              <button
                key={`${entry.humanoidId}-${entry.boneName}`}
                type="button"
                className="rounded bg-primary/10 px-2 py-0.5 text-xs text-primary"
                onClick={() => motionStore.selectBone(entry.humanoidId)}
              >
                {entry.boneName}
              </button>
            ))}
          </div>
        </div>
      )}
    </div>
  )
}
