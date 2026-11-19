import {
  RiAlertFill,
  RiDiscFill,
  RiGroup2Fill,
  RiMapPin2Fill,
  RiUserFollowFill,
} from '@remixicon/react'
import { Badge } from '@/components/ui/badge'
import { motionStore, useMotionStore } from '../store'

// Studio context header bar: right-aligned driver/bound actor, active scene, track,
// and resource warnings. Playback status & permission badges removed per streamlined header design.
export function MotionStatusBar() {
  const actors = useMotionStore((state) => state.actors)
  const player = useMotionStore((state) => state.player)
  const loaded = useMotionStore((state) => state.loaded)
  const library = useMotionStore((state) => state.library)
  const selectedKey = useMotionStore((state) => state.selectedLibraryKey)
  const permission = useMotionStore((state) => state.permission)
  const stageSceneName = useMotionStore((state) => state.stage.sceneName)
  const ensembleActive = useMotionStore((state) => state.ensemble.active)

  const targetName = actors.targetLabel || '未确认'
  const boundName = actors.boundLabel || '尚未绑定'
  let trackName = '未加载曲目'
  if (loaded.motionKey) {
    trackName = player.title
  } else if (selectedKey) {
    trackName = library.entries.find((entry) => entry.id === selectedKey)?.title ?? trackName
  }

  return (
    <header
      className="flex min-h-[38px] shrink-0 flex-wrap items-center justify-end gap-2.5 bg-background px-4 py-1.5 text-xs"
      data-testid="motion-status-bar"
    >
      {(loaded.sourceMissing || loaded.sourceChanged) && (
        <Badge variant="warning" className="gap-1 rounded-[4px]">
          <RiAlertFill className="size-3" />
          <span>{loaded.sourceMissing ? '源文件缺失' : '源文件已变更'}</span>
        </Badge>
      )}

      <span className="hidden items-center gap-1.5 text-muted-foreground lg:inline-flex">
        <RiDiscFill className="size-3.5 shrink-0" />
        <span className="max-w-[200px] truncate">曲目: {trackName}</span>
      </span>

      {stageSceneName && (
        <span className="hidden items-center gap-1 text-muted-foreground sm:inline-flex">
          <RiMapPin2Fill className="size-3" />
          <span className="max-w-[140px] truncate">{stageSceneName}</span>
        </span>
      )}

      {ensembleActive && (
        <Badge variant="outline" className="rounded-[4px] border-border bg-muted/60 px-1.5 py-0 text-[10px] text-foreground">
          多人舞台激活
        </Badge>
      )}

      {permission.injectionAllowed ? (
        <button
          type="button"
          onClick={() => motionStore.setPage('motion')}
          className="inline-flex items-center gap-1.5 rounded-md border border-border bg-background/60 px-2 py-0.5 text-foreground transition-colors hover:border-foreground/30 hover:bg-accent/55"
          title="点击前往动作页切换驱动角色"
        >
          {ensembleActive ? (
            <RiGroup2Fill className="size-3.5 text-primary" />
          ) : (
            <RiUserFollowFill className="size-3.5 text-primary" />
          )}
          <span className="font-medium">驱动: {targetName}</span>
          <span className="text-muted-foreground">· 绑定: {boundName}</span>
        </button>
      ) : (
        <div className="inline-flex items-center gap-1.5 rounded-md border border-border bg-background/60 px-2 py-0.5 text-foreground">
          <RiUserFollowFill className="size-3.5 text-muted-foreground" />
          <span>驱动: {targetName}</span>
          <span className="text-muted-foreground">· 绑定: {boundName}</span>
        </div>
      )}
    </header>
  )
}
