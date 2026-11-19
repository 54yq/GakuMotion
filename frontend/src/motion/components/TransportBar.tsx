import * as React from 'react'
import * as SliderPrimitive from '@radix-ui/react-slider'
import {
  RiAlertFill,
  RiCameraFill,
  RiCheckFill,
  RiDiscFill,
  RiEqualizerFill,
  RiOrderPlayFill,
  RiPauseFill,
  RiPlayFill,
  RiRepeatFill,
  RiRepeatOneFill,
  RiRewindFill,
  RiShuffleFill,
  RiSpeedFill,
  RiSpeedUpFill,
  RiStopFill,
  RiVolumeDownFill,
  RiVolumeMuteFill,
  RiVolumeUpFill,
} from '@remixicon/react'
import { Button } from '@/components/ui/button'
import { Popover, PopoverContent, PopoverTrigger } from '@/components/ui/popover'
import { Slider } from '@/components/ui/slider'
import { Tooltip, TooltipContent, TooltipTrigger } from '@/components/ui/tooltip'
import { cn } from '@/lib/utils'
import { CAMERA_MODES, ENSEMBLE_PLAY_MODES, PLAYBACK_SPEEDS, PLAY_MODES } from '../protocol'
import { motionStore, useMotionStore } from '../store'
import { durationMismatch, formatTransportTime, framePosition, validTimeline } from '../time'
import { PrecisionControlPanel } from './PrecisionControlPanel'
import { PendingBadge } from './Unsupported'

function IconTooltip({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <Tooltip>
      <TooltipTrigger asChild>{children}</TooltipTrigger>
      <TooltipContent side="top">{label}</TooltipContent>
    </Tooltip>
  )
}

function playModeIcon(mode: number, ensembleActive: boolean) {
  if (ensembleActive) {
    return mode === 1 ? RiRepeatFill : RiOrderPlayFill
  }
  if (mode === 1) return RiRepeatOneFill
  if (mode === 2) return RiRepeatFill
  if (mode === 3) return RiShuffleFill
  return RiOrderPlayFill
}

export function TransportBar() {
  const player = useMotionStore((state) => state.player)
  const scrub = useMotionStore((state) => state.scrub)
  const loaded = useMotionStore((state) => state.loaded)
  const libraryEntries = useMotionStore((state) => state.library.entries)
  const actors = useMotionStore((state) => state.actors)
  const ensembleActive = useMotionStore((state) => state.ensemble.active)
  const musicVolumeSupport = useMotionStore((state) => state.musicVolumeSupport)
  const musicVolumeNotice = useMotionStore((state) => state.musicVolumeNotice)
  const navigationError = useMotionStore((state) => state.navigationError)
  const actionError = useMotionStore((state) => state.actionError)
  const cameraPending = useMotionStore((state) => state.cameraPending)
  const capabilities = useMotionStore((state) => state.capabilities)
  const seekStepSeconds = useMotionStore((state) => state.seekStepSeconds)
  const precisionOpen = useMotionStore((state) => state.precisionPanelOpen)
  const precisionPinned = useMotionStore((state) => state.precisionPanelPinned)

  const [cameraOpen, setCameraOpen] = React.useState(false)
  const [speedOpen, setSpeedOpen] = React.useState(false)
  const [volumeOpen, setVolumeOpen] = React.useState(false)
  const [hoverInfo, setHoverInfo] = React.useState<{ ratio: number; seconds: number } | null>(null)
  const lastVolumeRef = React.useRef<number>(100)

  const supports = (method: string) => capabilities.includes(method)
  const hasClip = validTimeline(player.totalFrames, player.fps)
  const duration = player.duration > 0 ? player.duration : 0
  const scrubEnabled = hasClip && duration > 0 && supports('player.scrubBegin')
  const transportSupported = supports('player.play')

  const isPlaying = player.state === 'playing'
  const isPaused = player.state === 'paused'
  const isPreview = player.state === 'preview'

  const displayTime = scrub.active ? scrub.value : player.time
  const volumePercent = Math.round(player.volume * 100)
  React.useEffect(() => {
    if (volumePercent > 0) lastVolumeRef.current = volumePercent
  }, [volumePercent])

  const currentEntry = React.useMemo(
    () => libraryEntries.find((entry) => entry.id === loaded.motionKey) ?? null,
    [libraryEntries, loaded.motionKey],
  )
  const currentSubtitle = React.useMemo(() => {
    if (!currentEntry?.subtitle) return ''
    return currentEntry.subtitle
      .split(',')
      .map((s) => s.trim())
      .filter(Boolean)
      .join(' / ')
  }, [currentEntry?.subtitle])

  const cameraModeIndex = motionStore.cameraQuickModeIndex()
  const playModeLabels = ensembleActive ? ENSEMBLE_PLAY_MODES : PLAY_MODES
  const normalizedPlayMode =
    player.playMode >= 0 && player.playMode < playModeLabels.length ? player.playMode : 1
  const currentPlayModeLabel = playModeLabels[normalizedPlayMode] ?? playModeLabels[0]
  const PlayModeIcon = playModeIcon(normalizedPlayMode, ensembleActive)

  const hasDurationMismatch = hasClip && durationMismatch(player.totalFrames, player.fps, player.duration)
  const showSourceNotice = loaded.sourceMissing || loaded.sourceChanged
  const showVolumeNotice = musicVolumeSupport < 0 || musicVolumeNotice !== ''

  const handleTrackPointerMove = (event: React.PointerEvent<HTMLDivElement>) => {
    if (!hasClip || duration <= 0) return
    const rect = event.currentTarget.getBoundingClientRect()
    if (rect.width <= 0) return
    const ratio = Math.min(Math.max((event.clientX - rect.left) / rect.width, 0), 1)
    setHoverInfo({ ratio, seconds: ratio * duration })
  }

  const VolumeIcon = volumePercent === 0 ? RiVolumeMuteFill : volumePercent < 50 ? RiVolumeDownFill : RiVolumeUpFill
  const speedModified = Math.abs(player.speed - 1) > 0.01

  return (
    <footer
      className="relative z-30 shrink-0 bg-playerBar select-none"
      data-testid="transport-bar"
    >
      {/* Top Edge: Full-width precision timeline scrubber */}
      <div
        className="group relative -mt-1.5 flex h-3 w-full cursor-pointer items-center"
        onPointerMove={handleTrackPointerMove}
        onPointerLeave={() => setHoverInfo(null)}
      >
        <SliderPrimitive.Root
          aria-label="播放进度"
          className="relative flex h-full w-full touch-none select-none items-center data-[disabled]:cursor-not-allowed data-[disabled]:opacity-55"
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
            setTimeout(() => motionStore.scrubCommit(value), 0)
          }}
        >
          <SliderPrimitive.Track className="relative h-[2px] w-full grow overflow-hidden bg-border transition-[height] duration-150 group-hover:h-[4px]">
            <SliderPrimitive.Range className="absolute h-full bg-primary transition-colors" />
          </SliderPrimitive.Track>
          <SliderPrimitive.Thumb
            aria-label="播放进度"
            className={cn(
              'block size-3 rounded-full border-2 border-primary bg-background shadow-sm transition-transform focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring disabled:pointer-events-none',
              scrub.active
                ? 'scale-100 opacity-100'
                : 'scale-75 opacity-0 group-hover:scale-100 group-hover:opacity-100 focus-visible:scale-100 focus-visible:opacity-100',
            )}
          />
        </SliderPrimitive.Root>

        {hoverInfo && hasClip && (
          <div
            aria-hidden="true"
            className="pointer-events-none absolute -top-7 z-40 -translate-x-1/2 whitespace-nowrap rounded-md border bg-popover px-2 py-0.5 text-[11px] font-medium tabular-nums text-popover-foreground shadow-md"
            style={{ left: `${Math.min(Math.max(hoverInfo.ratio * 100, 5), 95)}%` }}
          >
            {formatTransportTime(hoverInfo.seconds)} / {formatTransportTime(duration)} · 帧{' '}
            {Math.floor(framePosition(hoverInfo.seconds, player.fps, player.totalFrames))}
          </div>
        )}
      </div>

      {/* Inline diagnostic strip when critical notices/errors are present */}
      {(actionError || showSourceNotice || (navigationError && !precisionOpen) || (hasDurationMismatch && !precisionOpen)) && (
        <div className="flex flex-wrap items-center gap-x-4 gap-y-1 border-b border-border/60 bg-muted/40 px-4 py-1 text-xs">
          {showSourceNotice && (
            <span className="flex items-center gap-1 text-warning">
              <RiAlertFill className="size-3.5 shrink-0" />
              {loaded.sourceMissing
                ? '曲目源文件已移除：请先恢复文件，再到曲库重新加载当前曲目。'
                : '曲目文件已更新：请到曲库重新加载当前曲目；音乐更新后需要重新加载才能开播。'}
            </span>
          )}
          {hasDurationMismatch && !precisionOpen && (
            <button
              type="button"
              onClick={() => motionStore.setPrecisionPanelOpen(true)}
              className="text-left text-warning underline-offset-2 hover:underline"
              data-testid="transport-duration-warning"
            >
              源动作头部时长与采样帧不一致（点击查看详情）
            </button>
          )}
          {navigationError && !precisionOpen && (
            <span className="text-destructive" role="alert" data-testid="transport-navigation-error">
              {navigationError}
            </span>
          )}
          {actionError && (
            <span className="ml-auto flex items-center gap-2 text-destructive" role="alert" data-testid="transport-action-error">
              <span>{actionError}</span>
              <button
                type="button"
                onClick={() => motionStore.clearActionError()}
                className="text-[11px] underline underline-offset-2"
              >
                忽略
              </button>
            </span>
          )}
        </div>
      )}

      {/* Main 3-Zone Player Bar */}
      <div className="flex items-center justify-between gap-3 px-4 py-2">
        {/* Left: Calm neutral icon + Title + Status + Time + Actor */}
        <div className="flex min-w-0 flex-1 items-center gap-2.5">
          <div
            aria-hidden="true"
            className={cn(
              'flex size-9 shrink-0 items-center justify-center rounded-md border',
              hasClip
                ? 'border-primary/25 bg-primary/10 text-primary'
                : 'border-border bg-muted/60 text-muted-foreground',
            )}
          >
            <RiDiscFill className="size-4" />
          </div>
          <div className="min-w-0 flex-1 space-y-0.5">
            <div className="flex items-center gap-2">
              <div
                className="min-w-0 truncate text-[13px] font-semibold leading-tight text-foreground"
                data-testid="transport-title"
              >
                {player.title}
              </div>
              {!supports('player.scrubBegin') && <PendingBadge />}
            </div>
            <div className="flex flex-wrap items-center gap-2 text-[11px] text-muted-foreground">
              <span className="truncate">
                {currentSubtitle || (ensembleActive ? '多人舞台' : actors.targetLabel || '未绑定角色')}
              </span>
            </div>
          </div>
        </div>

        {/* Center: PlayMode hot-toggle + Rewind + Play/Pause + Fast-Forward + Stop */}
        <div className="flex shrink-0 items-center gap-1">
          <IconTooltip label={`播放模式：${currentPlayModeLabel}（点击切换）`}>
            <Button
              type="button"
              variant="ghost"
              size="icon"
              className="size-8 rounded-md text-muted-foreground hover:bg-accent hover:text-foreground"
              aria-label="切换播放模式"
              data-testid="transport-playmode-toggle"
              disabled={!supports('playlist.setMode')}
              onClick={() => void motionStore.cyclePlayMode()}
            >
              <PlayModeIcon className="size-4" />
            </Button>
          </IconTooltip>

          <IconTooltip label={`快退 ${seekStepSeconds} 秒 (←)`}>
            <Button
              type="button"
              variant="ghost"
              size="icon"
              className="size-8 rounded-md text-foreground/85 hover:bg-accent"
              aria-label="快退"
              disabled={!hasClip || !supports('player.seek')}
              onClick={() => void motionStore.seekRelative(-seekStepSeconds)}
            >
              <RiRewindFill className="size-4" />
            </Button>
          </IconTooltip>

          <IconTooltip label={isPlaying ? '暂停 (Space / F9)' : isPaused || isPreview ? '继续播放 (Space / F9)' : '播放 (Space / F9)'}>
            <Button
              type="button"
              variant="default"
              size="icon"
              className="size-9 rounded-full shadow-sm transition-transform active:scale-95"
              aria-label={isPlaying ? '暂停' : '播放'}
              disabled={!hasClip || !transportSupported}
              onClick={() => void (isPlaying ? motionStore.pause() : motionStore.play())}
            >
              {isPlaying ? <RiPauseFill className="size-4" /> : <RiPlayFill className="size-4 translate-x-[0.5px]" />}
            </Button>
          </IconTooltip>

          <IconTooltip label={`快进 ${seekStepSeconds} 秒 (→)`}>
            <Button
              type="button"
              variant="ghost"
              size="icon"
              className="size-8 rounded-md text-foreground/85 hover:bg-accent"
              aria-label="快进"
              disabled={!hasClip || !supports('player.seek')}
              onClick={() => void motionStore.seekRelative(seekStepSeconds)}
            >
              <RiSpeedFill className="size-4" />
            </Button>
          </IconTooltip>

          <IconTooltip label="停止：归零并恢复游戏动作 (F10)">
            <Button
              type="button"
              variant="ghost"
              size="icon"
              className="size-8 rounded-md text-muted-foreground hover:bg-accent hover:text-foreground"
              aria-label="停止"
              disabled={!hasClip || !supports('player.stop')}
              onClick={() => void motionStore.stop()}
            >
              <RiStopFill className="size-4" />
            </Button>
          </IconTooltip>
          {!transportSupported && <PendingBadge />}
        </div>

        {/* Right: Camera Badge Popover, Speed Icon, Volume Icon, Precision Drawer Trigger */}
        <div className="flex flex-1 items-center justify-end gap-1.5">
          {/* Compact Camera Mode Badge (NetEase right-side badge aesthetic) */}
          <div className="flex items-center gap-1">
            <Popover open={cameraOpen} onOpenChange={setCameraOpen}>
              <PopoverTrigger asChild>
                <button
                  type="button"
                  aria-label="镜头快捷模式"
                  data-testid="camera-mode-badge"
                  disabled={!supports('camera.set')}
                  className="inline-flex h-6 items-center gap-1 rounded-[4px] border border-border bg-background/70 px-2 text-[11px] font-medium text-muted-foreground transition-colors hover:border-foreground/30 hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring disabled:pointer-events-none disabled:opacity-50"
                >
                  <RiCameraFill className="size-3 shrink-0" />
                  <span>{CAMERA_MODES[cameraModeIndex] ?? 'VMD'}</span>
                </button>
              </PopoverTrigger>
              <PopoverContent side="top" align="end" className="w-36 p-1">
                <div className="px-2 py-1 text-[10px] font-medium text-muted-foreground">镜头模式</div>
                {CAMERA_MODES.map((label, index) => {
                  const active = cameraModeIndex === index
                  return (
                    <button
                      key={label}
                      type="button"
                      role="menuitemradio"
                      aria-checked={active}
                      onClick={() => {
                        setCameraOpen(false)
                        void motionStore.setCameraQuickMode(index)
                      }}
                      className={cn(
                        'flex w-full items-center justify-between rounded px-2 py-1.5 text-left text-xs transition-colors hover:bg-accent',
                        active ? 'font-semibold text-primary' : 'text-foreground',
                      )}
                    >
                      <span>{label}</span>
                      {active && <RiCheckFill className="size-3.5 text-primary" />}
                    </button>
                  )
                })}
              </PopoverContent>
            </Popover>
            {cameraPending && (
              <span className="size-2 animate-pulse rounded-full bg-primary" title="正在应用镜头设置" />
            )}
            {!supports('camera.set') && <PendingBadge />}
          </div>

          {/* Speed Icon Only (Tooltip shows current value) */}
          <Popover open={speedOpen} onOpenChange={setSpeedOpen}>
            <IconTooltip label={`播放倍速: ${player.speed.toFixed(2)}x`}>
              <PopoverTrigger asChild>
                <Button
                  type="button"
                  variant={speedModified ? 'secondary' : 'ghost'}
                  size="icon"
                  aria-label="播放速度"
                  className={cn(
                    'size-7 rounded-md',
                    speedModified ? 'text-primary' : 'text-muted-foreground hover:text-foreground',
                  )}
                >
                  <RiSpeedUpFill className="size-4" />
                </Button>
              </PopoverTrigger>
            </IconTooltip>
            <PopoverContent side="top" align="end" className="w-60 space-y-2.5 p-3">
              <div className="flex items-center justify-between text-xs">
                <span className="font-semibold">播放倍速</span>
                <span className="font-semibold tabular-nums text-primary">{player.speed.toFixed(2)}x</span>
              </div>
              <div className="grid grid-cols-3 gap-1">
                {PLAYBACK_SPEEDS.map((speed) => {
                  const active = Math.abs(player.speed - speed) < 0.01
                  return (
                    <button
                      key={speed}
                      type="button"
                      disabled={!supports('player.setSpeed')}
                      onClick={() => void motionStore.setSpeed(speed)}
                      className={cn(
                        'rounded border px-2 py-1 text-xs tabular-nums transition-colors disabled:opacity-50',
                        active
                          ? 'border-primary bg-primary text-primary-foreground'
                          : 'border-border bg-background/60 text-muted-foreground hover:text-foreground',
                      )}
                    >
                      {speed.toFixed(1)}x
                    </button>
                  )
                })}
              </div>
              <div className="flex items-center gap-2 pt-0.5">
                <Slider
                  aria-label="快捷倍速"
                  min={0.1}
                  max={3}
                  step={0.05}
                  disabled={!supports('player.setSpeed')}
                  value={[player.speed]}
                  onValueChange={([value]) => void motionStore.setSpeed(Math.round(value * 100) / 100)}
                />
              </div>
            </PopoverContent>
          </Popover>

          {/* Volume Icon Only + Vertical Volume Popover */}
          <Popover open={volumeOpen} onOpenChange={setVolumeOpen}>
            <IconTooltip label={`音乐音量: ${volumePercent}%`}>
              <PopoverTrigger asChild>
                <Button
                  type="button"
                  variant={showVolumeNotice ? 'outline' : 'ghost'}
                  size="icon"
                  aria-label="音量控制"
                  className={cn(
                    'size-7 rounded-md',
                    showVolumeNotice ? 'text-warning' : 'text-muted-foreground hover:text-foreground',
                  )}
                >
                  <VolumeIcon className="size-4" />
                </Button>
              </PopoverTrigger>
            </IconTooltip>
            <PopoverContent
              side="top"
              align="center"
              className={cn(
                'flex flex-col items-center gap-2 p-2.5',
                showVolumeNotice ? 'w-44' : 'w-14',
              )}
            >
              <div className="flex h-28 w-full items-center justify-center py-1">
                <Slider
                  orientation="vertical"
                  aria-label="音乐音量"
                  min={0}
                  max={100}
                  step={1}
                  disabled={!supports('player.setVolume')}
                  value={[volumePercent]}
                  onValueChange={([value]) => motionStore.setVolume(value)}
                  onValueCommit={([value]) => motionStore.commitVolume(value)}
                />
              </div>
              <span className="text-[11px] font-medium tabular-nums text-muted-foreground">
                {volumePercent}%
              </span>
              <Button
                type="button"
                variant="ghost"
                size="icon"
                className="size-7 shrink-0 rounded-md"
                aria-label={volumePercent === 0 ? '取消静音' : '静音'}
                disabled={!supports('player.setVolume')}
                onClick={() => {
                  const next = volumePercent === 0 ? lastVolumeRef.current || 100 : 0
                  motionStore.commitVolume(next)
                }}
              >
                <VolumeIcon className="size-4" />
              </Button>
              {showVolumeNotice && (
                <p className="border-t border-border/60 pt-1.5 text-center text-[11px] leading-snug text-warning">
                  {musicVolumeSupport < 0
                    ? '当前音乐后端不支持独立音量，音乐仍以原音量播放。'
                    : musicVolumeNotice}
                </p>
              )}
            </PopoverContent>
          </Popover>

          {/* Precision Control Drawer Trigger */}
          <Popover open={precisionOpen} onOpenChange={(next) => motionStore.setPrecisionPanelOpen(next)}>
            <PopoverTrigger asChild>
              <Button
                type="button"
                variant={precisionOpen ? 'default' : 'outline'}
                size="icon"
                className="relative size-7 rounded-md"
                aria-label="精细控制"
                title="精细控制与专业定位面板"
                data-testid="precision-control-trigger"
              >
                <RiEqualizerFill className="size-3.5" />
                {(precisionPinned || navigationError) && !precisionOpen && (
                  <span
                    aria-hidden="true"
                    className={cn(
                      'absolute -right-0.5 -top-0.5 size-2 rounded-full',
                      navigationError ? 'bg-destructive' : 'bg-primary',
                    )}
                  />
                )}
              </Button>
            </PopoverTrigger>
            <PopoverContent side="top" align="end" pinned={precisionPinned} className="p-3.5">
              <PrecisionControlPanel onClose={() => motionStore.setPrecisionPanelOpen(false)} />
            </PopoverContent>
          </Popover>
        </div>
      </div>
    </footer>
  )
}
