import * as React from 'react'
import { RiSearchFill } from '@remixicon/react'
import { Badge } from '@/components/ui/badge'
import { Button } from '@/components/ui/button'
import { Checkbox } from '@/components/ui/checkbox'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { cn } from '@/lib/utils'
import { ALL_BONES, BONE_GROUPS, type BoneOverrideEntry } from '../protocol'
import { motionStore, useMotionStore } from '../store'
import { DragFloat } from './DragFloat'
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

  const [boneSearch, setBoneSearch] = React.useState('')
  const [onlyOverridden, setOnlyOverridden] = React.useState(false)

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

  const hasOverride = React.useCallback(
    (hid: number) => bone.overrides.some((entry) => entry.humanoidId === hid),
    [bone.overrides],
  )

  const filteredGroups = React.useMemo(() => {
    const needle = boneSearch.trim().toLowerCase()
    return BONE_GROUPS.map((group) => ({
      title: group.title,
      bones: group.bones.filter((boneDef) => {
        if (onlyOverridden && !hasOverride(boneDef.hid)) return false
        if (!needle) return true
        return (
          boneDef.labelZh.toLowerCase().includes(needle) ||
          boneDef.name.toLowerCase().includes(needle) ||
          String(boneDef.hid) === needle
        )
      }),
    })).filter((group) => group.bones.length > 0)
  }, [boneSearch, onlyOverridden, hasOverride])

  return (
    <div className="space-y-3" data-testid="bone-section">
      {/* Toolbar */}
      <div className="flex flex-wrap items-center gap-2">
        <Button
          variant={bone.masterEnabled ? 'secondary' : 'outline'}
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
      {bone.notice && <p className="text-xs text-muted-foreground">{bone.notice}</p>}

      <div className="grid gap-3 lg:grid-cols-[1.15fr_1fr]">
        {/* Left: Bone navigator with search & override filter */}
        <div className="flex flex-col rounded-lg border bg-background/60">
          <div className="flex flex-wrap items-center gap-2 border-b border-border/60 p-2.5">
            <div className="relative min-w-[140px] flex-1">
              <RiSearchFill className="pointer-events-none absolute left-2 top-1/2 size-3.5 -translate-y-1/2 text-muted-foreground" />
              <Input
                className="h-7 pl-7 text-xs"
                placeholder="搜索骨骼名称..."
                aria-label="搜索骨骼"
                value={boneSearch}
                onChange={(event) => setBoneSearch(event.target.value)}
              />
            </div>
            <label className="flex items-center gap-1.5 text-xs text-muted-foreground">
              <Checkbox checked={onlyOverridden} onCheckedChange={(checked) => setOnlyOverridden(checked === true)} />
              仅看已覆盖 ({bone.overrides.length})
            </label>
          </div>

          <div className="max-h-72 space-y-3 overflow-y-auto p-2.5">
            {filteredGroups.length === 0 ? (
              <p className="py-4 text-center text-xs text-muted-foreground">没有匹配的骨骼节点</p>
            ) : (
              filteredGroups.map((group) => (
                <div key={group.title}>
                  <p className="mb-1.5 text-[11px] font-semibold uppercase tracking-wide text-muted-foreground">
                    {group.title}
                  </p>
                  <div className="flex flex-wrap gap-1">
                    {group.bones.map((boneDef) => (
                      <button
                        key={boneDef.hid}
                        type="button"
                        onClick={() => motionStore.selectBone(boneDef.hid)}
                        className={cn(
                          'rounded-md px-2 py-1 text-xs transition-colors',
                          selectedHid === boneDef.hid
                            ? 'bg-primary text-primary-foreground shadow-sm'
                            : hasOverride(boneDef.hid)
                              ? 'bg-primary/15 font-medium text-primary'
                              : 'bg-muted/60 text-foreground/85 hover:bg-muted',
                        )}
                      >
                        {boneDef.labelZh}
                        {hasOverride(boneDef.hid) ? ' *' : ''}
                      </button>
                    ))}
                  </div>
                </div>
              ))
            )}
          </div>
        </div>

        {/* Right: Bone Euler inspector */}
        <div className="space-y-3 rounded-lg border bg-background/60 p-3.5" data-testid="bone-editor">
          <div className="flex items-center justify-between gap-2 border-b border-border/60 pb-2">
            <p className="text-sm font-semibold">
              {selected
                ? `${selectedDef?.labelZh ?? selected.boneName} (hid ${selected.humanoidId})${activeOverride ? '' : ' [未覆盖]'}`
                : '选择左侧骨骼以编辑'}
            </p>
            {activeOverride && <Badge variant="default">已覆盖</Badge>}
          </div>

          {selected && (
            <>
              <div className="flex flex-wrap gap-4">
                <label className="flex items-center gap-2 text-xs">
                  <Checkbox
                    checked={symmetrical}
                    onCheckedChange={(checked) => motionStore.setBoneSymmetrical(checked === true)}
                  />
                  对称联动
                </label>
                <label className="flex items-center gap-2 text-xs">
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
              </div>

              <div className="space-y-2">
                {ANGLES.map((angle) => (
                  <div key={angle.key} className="flex items-center gap-2">
                    <Label className="w-12 text-xs font-medium text-muted-foreground">{angle.label}</Label>
                    <DragFloat
                      ariaLabel={`${selected.boneName} ${angle.label}`}
                      className="flex-1"
                      inputClassName="h-8 w-full text-xs"
                      value={selected.eulerDeg[angle.key]}
                      min={-180}
                      max={180}
                      step={0.5}
                      decimals={1}
                      unit="°"
                      onChange={(val) => {
                        const next = { ...selected.eulerDeg, [angle.key]: val }
                        void motionStore.setBoneOverride(
                          selected.humanoidId,
                          next.x,
                          next.y,
                          next.z,
                          selected.spaceWorld,
                          selected.enabled,
                        )
                      }}
                      onCommit={(val) => {
                        const next = { ...selected.eulerDeg, [angle.key]: val }
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
                      variant="outline"
                      size="sm"
                      className="h-8 px-2 text-xs"
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
                    <Button
                      variant="outline"
                      size="sm"
                      className="h-8 px-2 text-xs"
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
                  </div>
                ))}
              </div>

              <div className="flex flex-wrap gap-2 pt-1">
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
        <div className="rounded-lg border bg-background/40 p-2.5">
          <p className="mb-1.5 text-xs font-semibold text-muted-foreground">活跃覆盖（{bone.overrides.length}）</p>
          <div className="flex flex-wrap gap-1.5">
            {bone.overrides.map((entry) => (
              <button
                key={`${entry.humanoidId}-${entry.boneName}`}
                type="button"
                className="rounded-md border border-primary/30 bg-primary/10 px-2 py-0.5 text-xs text-primary transition-colors hover:bg-primary/20"
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
