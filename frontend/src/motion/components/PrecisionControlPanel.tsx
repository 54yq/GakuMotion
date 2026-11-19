import * as React from 'react'
import {
  RiCloseFill,
  RiPushpinFill,
  RiRefreshFill,
  RiRestartFill,
  RiRewindStartFill,
  RiSkipBackFill,
  RiSkipBackMiniFill,
  RiSkipForwardFill,
  RiSkipForwardMiniFill,
  RiSpeedUpFill,
  RiUnpinFill,
} from '@remixicon/react'
import { Badge } from '@/components/ui/badge'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Slider } from '@/components/ui/slider'
import { cn } from '@/lib/utils'
import { PLAYBACK_SPEEDS } from '../protocol'
import { motionStore, useMotionStore } from '../store'
import { durationMismatch, formatTransportTime, framePosition, lastFrameTime, validTimeline } from '../time'
import { PendingBadge } from './Unsupported'

const SEEK_STEP_OPTIONS = [1, 5, 10, 15] as const

export function PrecisionControlPanel({ onClose }: { onClose: () => void }) {
  const player = useMotionStore((state) => state.player)
  const loaded = useMotionStore((state) => state.loaded)
  const ensembleActive = useMotionStore((state) => state.ensemble.active)
  const libraryEmpty = useMotionStore((state) => state.library.entries.length === 0)
  const navigationError = useMotionStore((state) => state.navigationError)
  const seekStepSeconds = useMotionStore((state) => state.seekStepSeconds)
  const pinned = useMotionStore((state) => state.precisionPanelPinned)
  const capabilities = useMotionStore((state) => state.capabilities)

  const supports = (method: string) => capabilities.includes(method)
  const hasClip = validTimeline(player.totalFrames, player.fps)
  const framePos = hasClip ? framePosition(player.time, player.fps, player.totalFrames) : 0
  const maxFrame = hasClip ? player.totalFrames - 1 : 0

  const [timeText, setTimeText] = React.useState('00:00.000')
  const [editingTime, setEditingTime] = React.useState(false)
  const [frameText, setFrameText] = React.useState('0')
  const [editingFrame, setEditingFrame] = React.useState(false)
  const identity = `${loaded.motionKey}:${loaded.generation}:${loaded.injectionSession}`

  React.useEffect(() => {
    setEditingTime(false)
    setEditingFrame(false)
  }, [identity])

  React.useEffect(() => {
    if (!editingTime) setTimeText(formatTransportTime(player.time))
  }, [player.time, editingTime])

  React.useEffect(() => {
    if (!editingFrame) setFrameText(String(Math.floor(framePos)))
  }, [framePos, editingFrame])

  const commitTime = () => {
    setEditingTime(false)
    void motionStore.seekTime(timeText)
  }

  const commitFrame = () => {
    setEditingFrame(false)
    void motionStore.seekFrame(frameText)
  }

  const stepSpeed = (delta: number) => {
    const next = Math.min(Math.max(Math.round((player.speed + delta) * 100) / 100, 0.1), 3)
    void motionStore.setSpeed(next)
  }

  return (
    <div className="w-[430px] max-w-[calc(100vw-24px)] space-y-3.5 text-xs" data-testid="precision-control-panel">
      {/* Header */}
      <div className="flex items-center justify-between gap-2 border-b border-border/70 pb-2.5">
        <div className="flex items-center gap-2">
          <span className="text-sm font-semibold text-foreground">精细播放控制</span>
          {hasClip && (
            <Badge variant="outline" className="rounded-[4px] bg-muted/60 text-[10px] tabular-nums text-muted-foreground">
              {Number(player.fps.toPrecision(6))} fps
            </Badge>
          )}
        </div>
        <div className="flex items-center gap-1">
          <Button
            type="button"
            variant={pinned ? 'secondary' : 'ghost'}
            size="icon"
            className="size-7"
            aria-label={pinned ? '取消固定面板' : '固定面板'}
            title={pinned ? '取消固定（点击外部自动收起）' : '固定面板（切换页面不关闭）'}
            onClick={() => motionStore.setPrecisionPanelPinned(!pinned)}
          >
            {pinned ? <RiUnpinFill className="size-3.5 text-primary" /> : <RiPushpinFill className="size-3.5" />}
          </Button>
          <Button
            type="button"
            variant="ghost"
            size="icon"
            className="size-7"
            aria-label="关闭精细控制"
            onClick={onClose}
          >
            <RiCloseFill className="size-4" />
          </Button>
        </div>
      </div>

      {/* Live frame readout */}
      <div className="rounded-lg border bg-muted/45 px-3 py-2">
        {hasClip ? (
          <div className="flex items-center justify-between gap-2">
            <span className="text-xs tabular-nums text-foreground" data-testid="transport-frame-info">
              {framePos === Math.floor(framePos)
                ? `当前位置：动作帧 ${Math.floor(framePos)} / ${maxFrame} · ${Number(player.fps.toPrecision(6))} fps`
                : `当前位置：帧间 ${framePos.toFixed(3)} / ${maxFrame} · ${Number(player.fps.toPrecision(6))} fps`}
            </span>
          </div>
        ) : (
          <span className="text-muted-foreground">尚未加载演出曲目</span>
        )}
        {hasClip && durationMismatch(player.totalFrames, player.fps, player.duration) && (
          <p className="mt-1.5 text-xs text-warning" data-testid="transport-duration-warning">
            源动作头部时长与采样帧不一致：头部 {player.duration.toFixed(3)} 秒；末采样{' '}
            {lastFrameTime(player.totalFrames, player.fps).toFixed(3)} 秒。
          </p>
        )}
      </div>

      {/* Frame stepping & seek step */}
      <div className="space-y-2">
        <div className="flex items-center justify-between gap-2">
          <span className="font-medium text-foreground">逐帧微调</span>
          {!supports('player.stepFrame') && <PendingBadge />}
        </div>
        <div className="grid grid-cols-4 gap-1.5">
          <Button
            type="button"
            variant="outline"
            size="sm"
            className="h-8 gap-1 px-2 text-xs"
            disabled={!hasClip || !supports('player.seekFrame')}
            onClick={() => void motionStore.seekFrame('0')}
          >
            <RiRewindStartFill className="size-3.5" />
            首帧
          </Button>
          <Button
            type="button"
            variant="outline"
            size="sm"
            className="h-8 gap-1 px-2 text-xs"
            disabled={!hasClip || !supports('player.stepFrame')}
            onClick={() => void motionStore.stepFrame(-1)}
          >
            <RiSkipBackMiniFill className="size-4" />
            上一帧
          </Button>
          <Button
            type="button"
            variant="outline"
            size="sm"
            className="h-8 gap-1 px-2 text-xs"
            disabled={!hasClip || !supports('player.stepFrame')}
            onClick={() => void motionStore.stepFrame(1)}
          >
            下一帧
            <RiSkipForwardMiniFill className="size-4" />
          </Button>
          <Button
            type="button"
            variant="outline"
            size="sm"
            className="h-8 gap-1 px-2 text-xs"
            disabled={!hasClip || !supports('player.seekFrame')}
            onClick={() => void motionStore.seekFrame(String(maxFrame))}
          >
            末帧
            <RiSkipForwardFill className="size-3.5" />
          </Button>
        </div>

        <div className="flex items-center justify-between gap-2 pt-1">
          <span className="text-muted-foreground">主控快进/快退步长</span>
          <div className="flex items-center gap-1" role="group" aria-label="快进快退步长">
            {SEEK_STEP_OPTIONS.map((step) => (
              <button
                key={step}
                type="button"
                aria-pressed={seekStepSeconds === step}
                onClick={() => motionStore.setSeekStepSeconds(step)}
                className={cn(
                  'rounded-md border px-2 py-0.5 text-[11px] tabular-nums transition-colors',
                  seekStepSeconds === step
                    ? 'border-primary bg-primary/15 font-medium text-primary'
                    : 'border-border bg-background/60 text-muted-foreground hover:text-foreground',
                )}
              >
                {step}s
              </button>
            ))}
          </div>
        </div>
      </div>

      {/* Exact time & frame seek */}
      <div className="space-y-2 border-t border-border/60 pt-3">
        <span className="block font-medium text-foreground">精确时间与动作帧定位</span>
        <div className="grid grid-cols-2 gap-2">
          <div className="space-y-1">
            <label className="text-[11px] text-muted-foreground" htmlFor="precision-time-input">
              时间 (秒或 mm:ss.mmm)
            </label>
            <div className="flex gap-1">
              <Input
                id="precision-time-input"
                className="h-8 px-2 text-xs tabular-nums"
                aria-label="时间定位"
                value={timeText}
                disabled={!hasClip || !supports('player.seek')}
                onFocus={() => setEditingTime(true)}
                onChange={(event) => setTimeText(event.target.value)}
                onKeyDown={(event) => {
                  if (event.key === 'Enter') commitTime()
                  else if (event.key === 'Escape') {
                    setEditingTime(false)
                    setTimeText(formatTransportTime(player.time))
                  }
                }}
              />
              <Button
                type="button"
                variant="secondary"
                size="sm"
                className="h-8 shrink-0 px-2.5 text-xs"
                disabled={!hasClip || !supports('player.seek')}
                onClick={commitTime}
              >
                定位
              </Button>
            </div>
          </div>

          <div className="space-y-1">
            <label className="text-[11px] text-muted-foreground" htmlFor="precision-frame-input">
              动作帧 (0 – {maxFrame})
            </label>
            <div className="flex gap-1">
              <Input
                id="precision-frame-input"
                className="h-8 px-2 text-xs tabular-nums"
                aria-label="动作帧定位"
                value={frameText}
                disabled={!hasClip || !supports('player.seekFrame')}
                onFocus={() => setEditingFrame(true)}
                onChange={(event) => setFrameText(event.target.value)}
                onKeyDown={(event) => {
                  if (event.key === 'Enter') commitFrame()
                  else if (event.key === 'Escape') {
                    setEditingFrame(false)
                    setFrameText(String(Math.floor(framePos)))
                  }
                }}
              />
              <Button
                type="button"
                variant="secondary"
                size="sm"
                className="h-8 shrink-0 px-2.5 text-xs"
                disabled={!hasClip || !supports('player.seekFrame')}
                onClick={commitFrame}
              >
                定位
              </Button>
            </div>
          </div>
        </div>

        {navigationError && (
          <div
            className="rounded-md border border-destructive/40 bg-destructive/10 px-2.5 py-1.5 text-xs text-destructive"
            role="alert"
            data-testid="transport-navigation-error"
          >
            {navigationError}
          </div>
        )}
      </div>

      {/* Fine playback speed */}
      <div className="space-y-2 border-t border-border/60 pt-3">
        <div className="flex items-center justify-between">
          <span className="flex items-center gap-1.5 font-medium text-foreground">
            <RiSpeedUpFill className="size-3.5 text-primary" />
            精细倍速调节
          </span>
          <span className="text-xs font-semibold tabular-nums text-primary">
            {player.speed.toFixed(2)}x
          </span>
        </div>

        <div className="flex items-center gap-2">
          <Button
            type="button"
            variant="outline"
            size="sm"
            className="h-7 px-2 text-[11px] tabular-nums"
            disabled={!supports('player.setSpeed') || player.speed <= 0.1}
            onClick={() => stepSpeed(-0.05)}
          >
            -0.05x
          </Button>
          <Slider
            aria-label="精细倍速"
            className="flex-1"
            min={0.1}
            max={3}
            step={0.05}
            disabled={!supports('player.setSpeed')}
            value={[player.speed]}
            onValueChange={([value]) => void motionStore.setSpeed(Math.round(value * 100) / 100)}
          />
          <Button
            type="button"
            variant="outline"
            size="sm"
            className="h-7 px-2 text-[11px] tabular-nums"
            disabled={!supports('player.setSpeed') || player.speed >= 3}
            onClick={() => stepSpeed(0.05)}
          >
            +0.05x
          </Button>
          <Button
            type="button"
            variant="ghost"
            size="icon"
            className="size-7"
            aria-label="恢复 1.0x 倍速"
            title="恢复 1.00x"
            disabled={!supports('player.setSpeed') || Math.abs(player.speed - 1) < 0.001}
            onClick={() => void motionStore.setSpeed(1)}
          >
            <RiRestartFill className="size-3.5" />
          </Button>
        </div>

        <div className="flex flex-wrap gap-1">
          {PLAYBACK_SPEEDS.map((speed) => {
            const active = Math.abs(player.speed - speed) < 0.01
            return (
              <button
                key={speed}
                type="button"
                disabled={!supports('player.setSpeed')}
                onClick={() => void motionStore.setSpeed(speed)}
                className={cn(
                  'rounded-md border px-2 py-0.5 text-[11px] tabular-nums transition-colors disabled:opacity-50',
                  active
                    ? 'border-primary bg-primary text-primary-foreground'
                    : 'border-border/80 bg-background/60 text-muted-foreground hover:text-foreground',
                )}
              >
                {speed.toFixed(1)}x
              </button>
            )
          })}
        </div>
      </div>

      {/* Secondary track controls (Play mode hot-toggle moved to Player Bar) */}
      <div className="space-y-2 border-t border-border/60 pt-3">
        <div className="flex items-center justify-between gap-2">
          <span className="font-medium text-foreground">曲目切换与重载</span>
          {ensembleActive && (
            <span className="text-[11px] text-warning">多人舞台激活时禁用上下曲</span>
          )}
        </div>
        <div className="flex flex-wrap items-center gap-1.5">
          <Button
            type="button"
            variant="outline"
            size="sm"
            className="h-8 gap-1 px-2.5 text-xs"
            aria-label="上一曲"
            disabled={ensembleActive || libraryEmpty || !supports('player.cycle')}
            onClick={() => void motionStore.cycle(-1)}
          >
            <RiSkipBackFill className="size-3.5" />
            上一曲
          </Button>
          <Button
            type="button"
            variant="outline"
            size="sm"
            className="h-8 gap-1 px-2.5 text-xs"
            aria-label="下一曲"
            disabled={ensembleActive || libraryEmpty || !supports('player.cycle')}
            onClick={() => void motionStore.cycle(1)}
          >
            <RiSkipForwardFill className="size-3.5" />
            下一曲
          </Button>
          <Button
            type="button"
            variant="outline"
            size="sm"
            className="h-8 gap-1 px-2.5 text-xs"
            disabled={loaded.motionKey === '' || loaded.sourceMissing || !supports('player.reload')}
            onClick={() => void motionStore.reloadMotion()}
          >
            <RiRefreshFill className="size-3.5" />
            重载当前曲目
          </Button>
        </div>
      </div>

      {/* Shortcuts summary */}
      <div className="border-t border-border/60 pt-2 text-[11px] text-muted-foreground">
        快捷键：Space 播放/暂停 · ←/→ 快退快进 · Shift+←/→ 逐帧 · F9/F10 全局热键
      </div>
    </div>
  )
}
