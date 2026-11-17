import { useMotionStore } from '../store'

// Mirrors DrawTopStatusBar: driver/bound actor, track, playback state, and the
// permission indicator. Read-only; the native snapshot is authoritative.
export function MotionStatusBar() {
  const actors = useMotionStore((state) => state.actors)
  const player = useMotionStore((state) => state.player)
  const loaded = useMotionStore((state) => state.loaded)
  const library = useMotionStore((state) => state.library)
  const selectedKey = useMotionStore((state) => state.selectedLibraryKey)
  const permission = useMotionStore((state) => state.permission)

  const targetName = actors.targetLabel || '未确认'
  const boundName = actors.boundLabel || '尚未绑定'
  let trackName = '未加载曲目'
  if (loaded.motionKey) {
    trackName = player.title
  } else if (selectedKey) {
    trackName = library.entries.find((entry) => entry.id === selectedKey)?.title ?? trackName
  }

  const stateText =
    player.state === 'playing' ? '播放中' : player.state === 'paused' ? '已暂停' : player.state === 'preview' ? '预览' : '已停止'
  const stateClass =
    player.state === 'playing'
      ? 'text-success'
      : player.state === 'paused'
        ? 'text-warning'
        : player.state === 'preview'
          ? 'text-primary'
          : 'text-muted-foreground'

  return (
    <div className="border-b px-3 py-2 text-sm" data-testid="motion-status-bar">
      <div className="flex flex-wrap items-center justify-between gap-x-4 gap-y-1">
        <div className="min-w-0 space-y-0.5">
          <div className="truncate">
            驱动: {targetName} <span className="text-muted-foreground">|</span> 当前绑定: {boundName}
          </div>
          <div className="truncate text-muted-foreground">曲目: {trackName}</div>
        </div>
        <div className="flex shrink-0 items-center gap-3">
          <span className={stateClass}>状态: {stateText}</span>
          <span className={permission.injectionAllowed ? 'text-success' : 'text-destructive'}>
            {permission.injectionAllowed ? '[允许控制]' : '[已禁用]'}
          </span>
        </div>
      </div>
    </div>
  )
}
