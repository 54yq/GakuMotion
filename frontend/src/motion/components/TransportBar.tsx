import * as React from 'react'
import { Pause, Play, SkipBack, SkipForward, Square } from 'lucide-react'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { Slider } from '@/components/ui/slider'
import { Tooltip, TooltipContent, TooltipTrigger } from '@/components/ui/tooltip'
import { motionStore, useMotionStore } from '../store'
import { CAMERA_QUICK_MODES, ENSEMBLE_PLAY_MODES, PLAYBACK_SPEEDS, PLAY_MODES } from '../protocol'
import {
  durationMismatch,
  formatTransportTime,
  framePosition,
  lastFrameTime,
  validTimeline,
} from '../time'
import { PendingBadge } from './Unsupported'

function IconTooltip({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <Tooltip>
      <TooltipTrigger asChild>{children}</TooltipTrigger>
      <TooltipContent side="top">{label}</TooltipContent>
    </Tooltip>
  )
}

export function TransportBar() {
  const player = useMotionStore((state) => state.player)
  const scrub = useMotionStore((state) => state.scrub)
  const ensembleActive = useMotionStore((state) => state.ensemble.active)
  const libraryEmpty = useMotionStore((state) => state.library.entries.length === 0)
  const loaded = useMotionStore((state) => state.loaded)
  const musicVolumeSupport = useMotionStore((state) => state.musicVolumeSupport)
  const musicVolumeNotice = useMotionStore((state) => state.musicVolumeNotice)
  const navigationError = useMotionStore((state) => state.navigationError)
  const actionError = useMotionStore((state) => state.actionError)
  const cameraPending = useMotionStore((state) => state.cameraPending)
  const capabilities = useMotionStore((state) => state.capabilities)

  const supports = (method: string) => capabilities.includes(method)
  const hasClip = validTimeline(player.totalFrames, player.fps)
  const duration = player.duration > 0 ? player.duration : 0
  const scrubEnabled = hasClip && duration > 0 && supports('player.scrubBegin')
  const transportSupported = supports('player.play')

  const isPlaying = player.state === 'playing'
  const isPaused = player.state === 'paused'
  const isPreview = player.state === 'preview'
  const statusLabel = isPlaying ? '[播放]' : isPaused ? '[暂停]' : isPreview ? '[预览]' : '[停止]'
  const statusClass = isPlaying
    ? 'text-success'
    : isPaused
      ? 'text-warning'
      : isPreview
        ? 'text-primary'
        : 'text-muted-foreground'

  const displayTime = scrub.active ? scrub.value : player.time
  const framePos = hasClip ? framePosition(player.time, player.fps, player.totalFrames) : 0
  const playMode = player.playMode >= 0 && player.playMode <= 3 ? player.playMode : 1
  const playModeLabels = ensembleActive ? ENSEMBLE_PLAY_MODES : PLAY_MODES

  // Time/frame drafts reset when the loaded motion identity changes, and never
  // show live values while the user is editing (ImGui navigation semantics).
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

  const volumePercent = Math.round(player.volume * 100)
  const speedLabel = `倍速: ${player.speed.toFixed(1)}x`
  const cameraModeIndex = motionStore.cameraQuickModeIndex()

  return (
    <div className="border-t bg-card/60 px-3 py-2" data-testid="transport-bar">
      {/* Row 1: title + time */}
      <div className="flex items-baseline justify-between gap-3">
        <div className={`min-w-0 truncate text-sm font-medium ${statusClass}`} data-testid="transport-title">
          {statusLabel} {player.title}
        </div>
        <div className="shrink-0 text-xs tabular-nums text-muted-foreground" data-testid="transport-time">
          {formatTransportTime(displayTime)} / {formatTransportTime(duration)}
        </div>
      </div>

      {/* Row 2: three-stage scrub slider */}
      <div className="mt-1.5 flex items-center gap-2">
        <Slider
          aria-label="播放进度"
          className="flex-1"
          min={0}
          max={duration > 0 ? duration : 1}
          step={0.001}
          disabled={!scrubEnabled}
          value={[Math.min(Math.max(displayTime, 0), duration > 0 ? duration : 1)]}
          onValueChange={([value]) => {
            if (!motionStore.getState().scrub.active) motionStore.scrubStart(value)
            else motionStore.scrubMove(value)
          }}
          onValueCommit={([value]) => {
            // Radix fires keyboard commits inside the state updater, before the
            // change callback. Defer so begin→update→end order holds for both
            // pointer drags and keyboard steps.
            setTimeout(() => motionStore.scrubCommit(value), 0)
          }}
        />
        {!supports('player.scrubBegin') && <PendingBadge />}
      </div>

      {/* Row 3: transport buttons + mode + speed + volume + camera quick mode */}
      <div className="mt-2 flex flex-wrap items-center gap-x-4 gap-y-2">
        <div className="flex items-center gap-1">
          <IconTooltip label="上一曲">
            <Button
              variant="outline"
              size="icon"
              className="size-8"
              aria-label="上一曲"
              disabled={ensembleActive || libraryEmpty || !supports('player.cycle')}
              onClick={() => void motionStore.cycle(-1)}
            >
              <SkipBack className="size-4" />
            </Button>
          </IconTooltip>
          <IconTooltip label={isPlaying ? '暂停' : isPaused || isPreview ? '继续（从当前位置续播）' : '播放'}>
            <Button
              variant="outline"
              size="icon"
              className="size-8"
              aria-label={isPlaying ? '暂停' : '播放'}
              disabled={!hasClip || !transportSupported}
              onClick={() => void (isPlaying ? motionStore.pause() : motionStore.play())}
            >
              {isPlaying ? <Pause className="size-4" /> : <Play className="size-4" />}
            </Button>
          </IconTooltip>
          <IconTooltip label="停止：归零并恢复游戏动作">
            <Button
              variant="outline"
              size="icon"
              className="size-8"
              aria-label="停止"
              disabled={!hasClip || !supports('player.stop')}
              onClick={() => void motionStore.stop()}
            >
              <Square className="size-4" />
            </Button>
          </IconTooltip>
          <IconTooltip label="下一曲">
            <Button
              variant="outline"
              size="icon"
              className="size-8"
              aria-label="下一曲"
              disabled={ensembleActive || libraryEmpty || !supports('player.cycle')}
              onClick={() => void motionStore.cycle(1)}
            >
              <SkipForward className="size-4" />
            </Button>
          </IconTooltip>
          {!transportSupported && <PendingBadge />}
        </div>

        <label className="flex items-center gap-1.5 text-xs text-muted-foreground">
          <Select
            value={String(ensembleActive ? Math.min(playMode, 1) : playMode)}
            disabled={!supports('playlist.setMode')}
            onValueChange={(value) => void motionStore.setPlayMode(Number(value))}
          >
            <SelectTrigger className="h-8 w-[110px] text-xs" aria-label="播放模式">
              <SelectValue />
            </SelectTrigger>
            <SelectContent>
              {playModeLabels.map((label, index) => (
                <SelectItem key={label} value={String(index)}>
                  {label}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
        </label>

        <Select
          value={String(player.speed)}
          disabled={!supports('player.setSpeed')}
          onValueChange={(value) => void motionStore.setSpeed(Number(value))}
        >
          <SelectTrigger className="h-8 w-[110px] text-xs" aria-label="倍速">
            <SelectValue>{speedLabel}</SelectValue>
          </SelectTrigger>
          <SelectContent>
            {PLAYBACK_SPEEDS.map((speed) => (
              <SelectItem key={speed} value={String(speed)}>
                {`倍速: ${speed.toFixed(1)}x`}
              </SelectItem>
            ))}
          </SelectContent>
        </Select>

        <div className="flex items-center gap-2">
          <span className="text-xs text-muted-foreground">音乐音量</span>
          <Slider
            aria-label="音乐音量"
            className="w-[120px]"
            min={0}
            max={100}
            step={1}
            disabled={!supports('player.setVolume')}
            value={[volumePercent]}
            onValueChange={([value]) => motionStore.setVolume(value)}
            onValueCommit={([value]) => motionStore.commitVolume(value)}
          />
          <span className="w-9 text-xs tabular-nums text-muted-foreground">{volumePercent}%</span>
        </div>

        <div className="flex items-center gap-1.5">
          <Select
            value={String(cameraModeIndex)}
            disabled={!supports('camera.set')}
            onValueChange={(value) => void motionStore.setCameraQuickMode(Number(value))}
          >
            <SelectTrigger className="h-8 w-[120px] text-xs" aria-label="镜头快捷模式">
              <SelectValue />
            </SelectTrigger>
            <SelectContent>
              {CAMERA_QUICK_MODES.map((label, index) => (
                <SelectItem key={label} value={String(index)}>
                  {label}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
          {cameraPending && <span className="text-xs text-muted-foreground">应用中…</span>}
          {!supports('camera.set') && <PendingBadge />}
        </div>
      </div>

      {/* Row 4: frame stepping + precise time/frame navigation */}
      <div className="mt-2 flex flex-wrap items-center gap-x-4 gap-y-2">
        <div className="flex items-center gap-1">
          <Button
            variant="outline"
            size="sm"
            className="h-8"
            disabled={!hasClip || !supports('player.stepFrame')}
            onClick={() => void motionStore.stepFrame(-1)}
          >
            上一帧
          </Button>
          <Button
            variant="outline"
            size="sm"
            className="h-8"
            disabled={!hasClip || !supports('player.stepFrame')}
            onClick={() => void motionStore.stepFrame(1)}
          >
            下一帧
          </Button>
        </div>

        <div className="flex items-center gap-1.5">
          <span className="text-xs text-muted-foreground">时间</span>
          <Input
            className="h-8 w-[120px] px-2 text-xs tabular-nums"
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
          <Button variant="outline" size="sm" className="h-8" disabled={!hasClip || !supports('player.seek')} onClick={commitTime}>
            定位
          </Button>
        </div>

        <div className="flex items-center gap-1.5">
          <span className="text-xs text-muted-foreground">动作帧</span>
          <Input
            className="h-8 w-[90px] px-2 text-xs tabular-nums"
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
          <Button variant="outline" size="sm" className="h-8" disabled={!hasClip || !supports('player.seekFrame')} onClick={commitFrame}>
            定位
          </Button>
        </div>
      </div>

      {/* Row 5: position info + warnings (only rendered when present) */}
      <div className="mt-1.5 space-y-0.5 text-xs">
        {hasClip && (
          <div className="text-muted-foreground" data-testid="transport-frame-info">
            {framePos === Math.floor(framePos)
              ? `当前位置：动作帧 ${Math.floor(framePos)} / ${player.totalFrames - 1} · ${Number(player.fps.toPrecision(6))} fps`
              : `当前位置：帧间 ${framePos.toFixed(3)} / ${player.totalFrames - 1} · ${Number(player.fps.toPrecision(6))} fps`}
          </div>
        )}
        {hasClip && durationMismatch(player.totalFrames, player.fps, player.duration) && (
          <div className="text-warning" data-testid="transport-duration-warning">
            源动作头部时长与采样帧不一致：头部 {player.duration.toFixed(3)} 秒；末采样{' '}
            {lastFrameTime(player.totalFrames, player.fps).toFixed(3)} 秒。
          </div>
        )}
        {(loaded.sourceMissing || loaded.sourceChanged) && (
          <div className="text-warning">
            {loaded.sourceMissing
              ? '曲目源文件已移除：请先恢复文件，再到曲库重新加载当前曲目。'
              : '曲目文件已更新：请到曲库重新加载当前曲目；音乐更新后需要重新加载才能开播。'}
          </div>
        )}
        {(musicVolumeSupport < 0 || musicVolumeNotice !== '') && (
          <div className="text-warning">
            {musicVolumeSupport < 0 ? '当前音乐后端不支持独立音量，音乐仍以原音量播放。' : musicVolumeNotice}
          </div>
        )}
        {navigationError && (
          <div className="text-destructive" role="alert" data-testid="transport-navigation-error">
            {navigationError}
          </div>
        )}
        {actionError && (
          <div className="text-destructive" role="alert" data-testid="transport-action-error">
            {actionError}
          </div>
        )}
      </div>
    </div>
  )
}
