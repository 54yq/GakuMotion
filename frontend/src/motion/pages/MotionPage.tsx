import * as React from 'react'
import {
  RiBodyScanFill,
  RiEmotionHappyFill,
  RiGroup2Fill,
  RiMapPin2Fill,
  RiRestartFill,
  RiSave3Fill,
  RiUserFollowFill,
} from '@remixicon/react'
import { Badge } from '@/components/ui/badge'
import { Button } from '@/components/ui/button'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { Slider } from '@/components/ui/slider'
import { cn } from '@/lib/utils'
import type { FacialWeights } from '../protocol'
import { motionStore, useMotionStore } from '../store'
import { BoneSection } from '../components/BoneSection'
import { EnsembleSection } from '../components/EnsembleSection'
import { ForkEnsembleControls, ForkPlacementFields, type ForkCommand } from '../components/ForkWorkspace'
import { ForkAdvancedMotion } from '../components/ForkExtensions'
import type { ForkState } from '../protocol'
import { Vector3Field } from '../components/NumberField'
import { CollapsiblePropertySection, PropertyRow, PropertySection } from '../components/PropertyGroup'
import { PendingBadge } from '../components/Unsupported'

const FACIAL_SLIDERS: Array<{ key: keyof FacialWeights; label: string; hint: string }> = [
  { key: 'overall', label: '整体', hint: '总倍率' },
  { key: 'brow', label: '眉毛', hint: '眉峰与皱眉' },
  { key: 'eye', label: '眼部', hint: '眨眼与眼神开合' },
  { key: 'mouth', label: '嘴部', hint: '口型与发声形态' },
]

function FacialSliderRow({ facet, label, hint }: { facet: keyof FacialWeights; label: string; hint: string }) {
  const weights = useMotionStore((state) => state.facial.weights)
  const supported = useMotionStore((state) => state.capabilities.includes('facial.setWeights'))
  const [draft, setDraft] = React.useState<number | null>(null)
  const value = draft ?? weights[facet]
  return (
    <div className="space-y-1.5 px-3.5 py-2.5">
      <div className="flex items-center justify-between text-xs">
        <div className="flex items-baseline gap-2">
          <span className="font-medium text-foreground">{label}</span>
          <span className="text-[11px] text-muted-foreground">{hint}</span>
        </div>
        <span className="font-medium tabular-nums text-primary">{value.toFixed(2)} 倍</span>
      </div>
      <Slider
        aria-label={`表情幅度 ${label}`}
        min={0}
        max={2}
        step={0.01}
        disabled={!supported}
        value={[value]}
        onValueChange={([next]) => {
          setDraft(next)
          motionStore.setFacialWeights({ ...weights, [facet]: next })
        }}
        onValueCommit={([next]) => {
          setDraft(null)
          motionStore.commitFacialWeights({ ...motionStore.getState().facial.weights, [facet]: next })
        }}
      />
    </div>
  )
}

export function MotionPage({ fork, forkCommand }: { fork?: ForkState | null; forkCommand?: ForkCommand } = {}) {
  const actors = useMotionStore((state) => state.actors)
  const facial = useMotionStore((state) => state.facial)
  const position = useMotionStore((state) => state.position)
  const ensemble = useMotionStore((state) => state.ensemble)
  const boneCount = useMotionStore((state) => state.bone.overrides.length)
  const capabilities = useMotionStore((state) => state.capabilities)
  const supports = (method: string) => capabilities.includes(method)
  const forkBound = fork?.followers.filter(follower => follower.actorToken > 0).length ?? 0

  const selectedActor = actors.actors.find((actor) => actor.selected)
  const actorLabel = actors.actors.length === 0 ? '当前场景没有可驱动角色' : actors.targetLabel || '正在识别角色'

  return (
    <div className="mx-auto max-w-5xl space-y-4" data-testid="motion-page">
      {!ensemble.active && (
        <div className="grid items-start gap-4 lg:grid-cols-2">
          {/* Drive Actor & Stage Position */}
          <PropertySection
            title={
              <span className="inline-flex items-center gap-1.5">
                <RiUserFollowFill className="size-3.5 text-primary" />
                <span>驱动角色与初始站位</span>
              </span>
            }
            description="选择当前演出绑定的摄影角色，并调整世界坐标初始偏移"
            badge={!supports('actors.selectTarget') ? <PendingBadge /> : undefined}
          >
            <PropertyRow label="驱动角色" description="表情随驱动角色自动适配">
              <Select
                value={selectedActor ? String(selectedActor.token) : undefined}
                disabled={actors.actors.length === 0 || !supports('actors.selectTarget')}
                onValueChange={(value) => void motionStore.selectActor(Number(value))}
              >
                <SelectTrigger className="h-8 w-[190px] text-xs" aria-label="驱动角色">
                  <SelectValue placeholder={actorLabel} />
                </SelectTrigger>
                <SelectContent>
                  {actors.actors.map((actor) => (
                    <SelectItem key={actor.token} value={String(actor.token)}>
                      {actor.label}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
              <span className="rounded-[4px] border border-border bg-muted/60 px-2 py-0.5 text-[11px] text-muted-foreground">
                当前绑定：{actors.boundLabel || '尚未绑定'}
              </span>
            </PropertyRow>

            <div className="space-y-2.5 px-3.5 py-3">
              <div className="flex items-center justify-between gap-2">
                <div className="flex items-center gap-1.5 text-[13px] font-medium text-foreground">
                  <RiMapPin2Fill className="size-3.5 text-primary" />
                  <span>初始角色位置偏移</span>
                </div>
                <span className="text-[11px] text-muted-foreground">世界坐标偏移 (米)</span>
              </div>

              {fork && forkCommand ? (
                <ForkPlacementFields key={selectedActor?.token} labelPrefix="初始位置" slot={-1} token={selectedActor?.token ?? 0}
                  generation={0} initial={fork.position} yaw={fork.yaw} command={forkCommand} />
              ) : <Vector3Field
                labelPrefix="初始位置"
                value={position.offset}
                min={-10}
                max={10}
                step={0.01}
                disabled={!supports('position.set')}
                onChange={(next) => void motionStore.setPosition(next)}
                onCommit={(next) => void motionStore.setPosition(next)}
              />}

              <div className="flex flex-wrap items-center gap-1.5 pt-0.5">
                <Button
                  variant="outline"
                  size="sm"
                  className="h-7 px-2.5 text-xs"
                  disabled={fork ? !selectedActor : !supports('position.set')}
                  onClick={() => {
                    if (fork && forkCommand && selectedActor) void forkCommand('formation.place', {
                      slot: -1, actorToken: selectedActor.token, position: { x: 0, y: 0, z: 0 }, yaw: 0,
                    })
                    else motionStore.resetPosition()
                  }}
                >
                  <RiRestartFill className="size-3.5" />
                  {fork ? '重置站位与朝向' : '重置位置偏移'}
                </Button>
                <Button
                  variant="secondary"
                  size="sm"
                  className="h-7 px-2.5 text-xs"
                  disabled={!supports('position.save')}
                  onClick={() => void motionStore.savePosition()}
                >
                  <RiSave3Fill className="size-3.5" />
                  保存初始位置为默认
                </Button>
              </div>
              {position.notice && (
                <p className={cn('text-xs', /失败|无效/.test(position.notice) ? 'text-destructive' : 'text-muted-foreground')}>
                  {position.notice}
                </p>
              )}
              <p className="text-[11px] leading-relaxed text-muted-foreground">
                修改立即作用于当前驱动对象，切歌保留，停止后恢复场景原位；保存后作为下次启动的默认偏移。
              </p>
            </div>
          </PropertySection>

          {/* Facial Weight Mixer */}
          <CollapsiblePropertySection
            title={
              <span className="inline-flex items-center gap-1.5">
                <RiEmotionHappyFill className="size-3.5 text-primary" />
                <span>表情幅度</span>
              </span>
            }
            description="分项与整体倍率相乘；通常限制在 0–100%。眼骨视线不受影响。"
            badge={!supports('facial.setWeights') ? <PendingBadge /> : undefined}
            className="self-start"
          >
            <div className="mb-2 flex flex-wrap gap-1.5">
              <Button
                variant="outline"
                size="sm"
                className="h-7 px-2 text-xs"
                disabled={!supports('facial.setWeights')}
                onClick={() => motionStore.resetFacialWeights()}
              >
                重置表情幅度
              </Button>
              <Button
                variant="secondary"
                size="sm"
                className="h-7 px-2 text-xs"
                disabled={!supports('facial.save')}
                onClick={() => void motionStore.saveFacialWeights()}
              >
                保存表情幅度
              </Button>
            </div>
            {FACIAL_SLIDERS.map((slider) => (
              <FacialSliderRow key={slider.key} facet={slider.key} label={slider.label} hint={slider.hint} />
            ))}
            {facial.notice && (
              <div className="px-3.5 py-2 text-xs text-primary">{facial.notice}</div>
            )}
          </CollapsiblePropertySection>
        </div>
      )}

      {/* Ensemble Configuration */}
      <CollapsiblePropertySection
        title={
          <span className="inline-flex items-center gap-1.5">
            <RiGroup2Fill className="size-3.5 text-primary" />
            <span>{fork ? '多人舞台' : '多人舞台（最多3人）'}</span>
          </span>
        }
        description={fork ? '按场景识别的模型添加角色槽，独立指定动作、站位与朝向' : '编排最多 3 名角色同台演出，独立指定动作、站位偏移与朝向'}
        badge={
          !fork && !supports('ensemble.set') ? (
            <PendingBadge />
          ) : ensemble.active ? (
            <Badge variant="outline" className="rounded-[4px] border-success/40 bg-success/10 text-success">
              已激活
            </Badge>
          ) : undefined
        }
        summary={fork ? `已识别 ${actors.actors.length} · 副角色 ${forkBound}` : ensemble.active ? '当前运行中' : '最多 3 槽位'}
        defaultOpen={ensemble.active || forkBound > 0}
        testId="motion-ensemble"
      >
        {fork && forkCommand ? <ForkEnsembleControls state={fork} command={forkCommand} /> : <EnsembleSection />}
      </CollapsiblePropertySection>

      {/* Advanced Bone Tuning */}
      <CollapsiblePropertySection
        title={
          <span className="inline-flex items-center gap-1.5">
            <RiBodyScanFill className="size-3.5 text-primary" />
            <span>高级动作设置</span>
          </span>
        }
        description="51 块人形骨骼本地/世界坐标角度微调、左右对称联动与单曲/全局持久化"
        badge={!supports('bone.set') ? <PendingBadge /> : undefined}
        summary={boneCount > 0 ? `${boneCount} 项活跃覆盖` : '暂无覆盖'}
      >
        <div className="space-y-3">
          {fork && forkCommand && <ForkAdvancedMotion state={fork} command={forkCommand} />}
          <BoneSection />
        </div>
      </CollapsiblePropertySection>
    </div>
  )
}
