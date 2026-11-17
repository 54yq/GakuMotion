import * as React from 'react'
import { ChevronDown } from 'lucide-react'
import { Button } from '@/components/ui/button'
import { Collapsible, CollapsibleContent, CollapsibleTrigger } from '@/components/ui/collapsible'
import { Label } from '@/components/ui/label'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { Separator } from '@/components/ui/separator'
import { Slider } from '@/components/ui/slider'
import { motionStore, useMotionStore } from '../store'
import type { FacialWeights } from '../protocol'
import { Vector3Field } from '../components/NumberField'
import { PendingBadge } from '../components/Unsupported'
import { EnsembleSection } from '../components/EnsembleSection'
import { BoneSection } from '../components/BoneSection'

const FACIAL_SLIDERS: Array<{ key: keyof FacialWeights; label: string }> = [
  { key: 'overall', label: '整体' },
  { key: 'brow', label: '眉毛' },
  { key: 'eye', label: '眼部' },
  { key: 'mouth', label: '嘴部' },
]

function FacialSlider({ facet, label }: { facet: keyof FacialWeights; label: string }) {
  const weights = useMotionStore((state) => state.facial.weights)
  const supported = useMotionStore((state) => state.capabilities.includes('facial.setWeights'))
  const [draft, setDraft] = React.useState<number | null>(null)
  const value = draft ?? weights[facet]
  return (
    <div className="flex items-center gap-3">
      <span className="w-10 shrink-0 text-sm text-muted-foreground">{label}</span>
      <Slider
        aria-label={`表情幅度 ${label}`}
        className="max-w-xs flex-1"
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
      <span className="w-16 text-sm tabular-nums text-muted-foreground">{value.toFixed(2)} 倍</span>
    </div>
  )
}

export function MotionPage() {
  const actors = useMotionStore((state) => state.actors)
  const facial = useMotionStore((state) => state.facial)
  const position = useMotionStore((state) => state.position)
  const ensemble = useMotionStore((state) => state.ensemble)
  const capabilities = useMotionStore((state) => state.capabilities)
  const supports = (method: string) => capabilities.includes(method)

  const selectedActor = actors.actors.find((actor) => actor.selected)
  const actorLabel = actors.actors.length === 0 ? '当前场景没有可驱动角色' : actors.targetLabel || '正在识别角色'

  return (
    <div className="space-y-5" data-testid="motion-page">
      {/* Ensemble (skeleton until native ensemble.* lands) */}
      <Collapsible defaultOpen={ensemble.active}>
        <CollapsibleTrigger className="flex items-center gap-1.5 text-sm font-medium">
          <ChevronDown className="size-4" />
          多人舞台（最多3人）
          {!supports('ensemble.set') && <PendingBadge />}
        </CollapsibleTrigger>
        <CollapsibleContent className="mt-2">
          <EnsembleSection />
        </CollapsibleContent>
      </Collapsible>

      {!ensemble.active && (
        <>
          <Separator />
          {/* Drive actor */}
          <section className="space-y-2">
            <h3 className="text-sm font-medium">驱动角色</h3>
            <div className="flex items-center gap-2">
              <Select
                value={selectedActor ? String(selectedActor.token) : undefined}
                disabled={actors.actors.length === 0 || !supports('actors.selectTarget')}
                onValueChange={(value) => void motionStore.selectActor(Number(value))}
              >
                <SelectTrigger className="h-8 w-[300px]" aria-label="驱动角色">
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
              {!supports('actors.selectTarget') && <PendingBadge />}
            </div>
            <p className="text-xs text-muted-foreground">表情随驱动角色自动适配。</p>
          </section>

          <Separator />
          {/* Facial weights */}
          <section className="space-y-2">
            <h3 className="text-sm font-medium">表情幅度</h3>
            {FACIAL_SLIDERS.map((slider) => (
              <FacialSlider key={slider.key} facet={slider.key} label={slider.label} />
            ))}
            <p className="text-xs text-muted-foreground">
              分项与整体倍率相乘；通常限制在 0–100%。眼骨视线不受影响。
            </p>
            <div className="flex items-center gap-2">
              <Button variant="outline" size="sm" disabled={!supports('facial.setWeights')} onClick={() => motionStore.resetFacialWeights()}>
                重置表情幅度
              </Button>
              <Button variant="outline" size="sm" disabled={!supports('facial.save')} onClick={() => void motionStore.saveFacialWeights()}>
                保存表情幅度
              </Button>
            </div>
            {facial.notice && <p className="text-sm text-muted-foreground">{facial.notice}</p>}
          </section>

          <Separator />
          {/* Initial position */}
          <section className="space-y-2">
            <h3 className="text-sm font-medium">初始角色位置偏移</h3>
            <Label className="text-xs text-muted-foreground">世界坐标偏移 (米)</Label>
            <Vector3Field
              labelPrefix="初始位置"
              value={position.offset}
              min={-10}
              max={10}
              step={0.01}
              disabled={!supports('position.set')}
              onCommit={(next) => void motionStore.setPosition(next)}
            />
            <div className="flex items-center gap-2">
              <Button variant="outline" size="sm" disabled={!supports('position.set')} onClick={() => motionStore.resetPosition()}>
                重置位置偏移
              </Button>
              <Button variant="outline" size="sm" disabled={!supports('position.save')} onClick={() => void motionStore.savePosition()}>
                保存初始位置为默认
              </Button>
            </div>
            {position.notice && (
              <p className={`text-sm ${/失败|无效/.test(position.notice) ? 'text-destructive' : 'text-muted-foreground'}`}>
                {position.notice}
              </p>
            )}
            <p className="text-xs text-muted-foreground">
              修改立即作用于当前驱动对象，切歌保留，停止后恢复场景原位；保存后作为下次启动的默认偏移。
            </p>
          </section>
        </>
      )}

      <Separator />
      {/* Advanced: bone tuning skeleton */}
      <Collapsible>
        <CollapsibleTrigger className="flex items-center gap-1.5 text-sm font-medium">
          <ChevronDown className="size-4" />
          高级动作设置
          {!supports('bone.set') && <PendingBadge />}
        </CollapsibleTrigger>
        <CollapsibleContent className="mt-2">
          <BoneSection />
        </CollapsibleContent>
      </Collapsible>
    </div>
  )
}
