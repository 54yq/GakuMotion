import * as React from 'react'
import {
  RiAddFill,
  RiArrowDownFill,
  RiArrowUpDownFill,
  RiArrowUpFill,
  RiDeleteBinFill,
  RiDeleteBinLine,
  RiEyeFill,
  RiEyeLine,
  RiFilter2Fill,
  RiFolderOpenFill,
  RiMoreFill,
  RiPauseFill,
  RiPencilFill,
  RiPencilLine,
  RiPlayFill,
  RiRefreshFill,
  RiSearchLine,
} from '@remixicon/react'
import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
} from '@/components/ui/alert-dialog'
import { Button } from '@/components/ui/button'
import {
  DropdownMenu,
  DropdownMenuCheckboxItem,
  DropdownMenuContent,
  DropdownMenuLabel,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from '@/components/ui/dropdown-menu'
import { Input } from '@/components/ui/input'
import { Popover, PopoverContent, PopoverTrigger } from '@/components/ui/popover'
import { cn } from '@/lib/utils'
import type { ForkState, LibraryEntry, PlaybackStatus } from '../protocol'
import type { ForkCommand } from '../components/ForkWorkspace'
import { motionStore, useMotionStore, type LibraryFilter } from '../store'
import { formatMmSs } from '../time'
import { ContextMenu, type ContextMenuEntry } from '../components/ContextMenu'
import { PendingBadge } from '../components/Unsupported'
import { ImportDialog } from './ImportDialog'

const ROW_HEIGHT = 50
const VIRTUALIZE_THRESHOLD = 200

const FILTER_OPTIONS: ReadonlyArray<{ value: LibraryFilter; label: string }> = [
  { value: 'all', label: '全部曲目' },
  { value: 'camera', label: '含镜头' },
  { value: 'audio', label: '含音乐' },
  { value: 'facial', label: '含表情' },
  { value: 'motionOnly', label: '纯动作' },
]

function trackTags(entry: LibraryEntry): string[] {
  const tags: string[] = []
  if (entry.hasMotion) tags.push('动作')
  if (entry.hasFacial) tags.push('表情')
  if (entry.hasCamera) tags.push('镜头')
  if (entry.hasAudio) tags.push('音频')
  return tags
}

function formatSubtitle(raw?: string): string {
  if (!raw) return ''
  const parts = raw
    .split(',')
    .map((part) => part.trim())
    .filter(Boolean)
  return parts.join(' / ')
}

function PlayingEqualizer() {
  return (
    <span className="inline-flex h-3.5 items-end gap-[2px]" aria-hidden="true">
      <span className="w-[2px] h-full rounded-full bg-primary animate-eq-1" />
      <span className="w-[2px] h-full rounded-full bg-primary animate-eq-2" />
      <span className="w-[2px] h-full rounded-full bg-primary animate-eq-3" />
    </span>
  )
}

interface LibraryRowProps {
  entry: LibraryEntry
  index: number
  selected: boolean
  loadedStatus: PlaybackStatus | null
  busy: boolean
  canSelect: boolean
  canPause: boolean
  canEdit: boolean
  canDelete: boolean
  style?: React.CSSProperties
  onSelect: () => void
  onDoubleClick: () => void
  onContextMenu: (event: React.MouseEvent<HTMLDivElement>) => void
  onPlay: () => void
  onPause: () => void
  onPreview: () => void
  onEdit: () => void
  onDelete: () => void
}

function LibraryRow({
  entry,
  index,
  selected,
  loadedStatus,
  busy,
  canSelect,
  canPause,
  canEdit,
  canDelete,
  style,
  onSelect,
  onDoubleClick,
  onContextMenu,
  onPlay,
  onPause,
  onPreview,
  onEdit,
  onDelete,
}: LibraryRowProps) {
  const tags = trackTags(entry)
  const subtitleText = formatSubtitle(entry.subtitle)
  const isPlaying = loadedStatus === 'playing'
  const isPaused = loadedStatus === 'paused' || loadedStatus === 'preview'

  return (
    <div
      role="row"
      aria-selected={selected}
      data-testid={`library-row-${entry.id}`}
      style={style}
      onClick={(event) => {
        event.stopPropagation()
        onSelect()
      }}
      onDoubleClick={(event) => {
        event.stopPropagation()
        onDoubleClick()
      }}
      onContextMenu={onContextMenu}
      className={cn(
        'group relative flex h-[50px] select-none items-center gap-3 rounded-lg px-3 text-left transition-all duration-150',
        selected
          ? 'bg-card shadow-[0_2px_8px_rgba(0,0,0,0.06)] dark:shadow-[0_2px_10px_rgba(0,0,0,0.28)]'
          : 'hover:bg-card hover:shadow-[0_2px_8px_rgba(0,0,0,0.06)] dark:hover:shadow-[0_2px_10px_rgba(0,0,0,0.28)]',
      )}
    >
      {/* 状态 / 序号：仅响应 Hover（click 后主动 blur，避免鼠标移开后停留在暂停按钮） */}
      <div
        role="cell"
        className="relative flex w-8 shrink-0 items-center justify-center"
        onClick={(event) => event.stopPropagation()}
        onDoubleClick={(event) => event.stopPropagation()}
      >
        <span
          className={cn(
            'flex items-center justify-center transition-opacity group-hover:opacity-0',
            isPlaying || isPaused ? 'text-primary' : 'text-xs font-medium tabular-nums text-muted-foreground',
          )}
        >
          {isPlaying ? (
            <PlayingEqualizer />
          ) : isPaused ? (
            <RiPlayFill className="size-4" />
          ) : (
            String(index + 1).padStart(2, '0')
          )}
        </span>

        {isPlaying ? (
          <button
            type="button"
            disabled={busy || !canPause}
            aria-label={`暂停 ${entry.title}`}
            title="暂停"
            onClick={(event) => {
              event.currentTarget.blur()
              onPause()
            }}
            className="absolute inset-0 flex items-center justify-center rounded-md text-primary opacity-0 transition-opacity group-hover:opacity-100 focus-visible:opacity-100 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring disabled:pointer-events-none disabled:opacity-40"
          >
            <RiPauseFill className="size-4" />
          </button>
        ) : (
          <button
            type="button"
            disabled={busy || !canSelect}
            aria-label={`播放 ${entry.title}`}
            title={isPaused ? '继续播放' : '播放'}
            onClick={(event) => {
              event.currentTarget.blur()
              onPlay()
            }}
            className={cn(
              'absolute inset-0 flex items-center justify-center rounded-md opacity-0 transition-opacity group-hover:opacity-100 focus-visible:opacity-100 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring disabled:pointer-events-none disabled:opacity-40',
              isPaused ? 'text-primary' : 'text-foreground hover:text-primary',
            )}
          >
            <RiPlayFill className="size-4" />
          </button>
        )}
      </div>

      {/* 演出曲目 + 右侧 3 个空心悬停小图标（正在播放时标题与小标题同时高亮为主色） */}
      <div role="cell" className="flex min-w-0 flex-1 items-center justify-between gap-2">
        <div className="min-w-0 flex-1">
          <div
            className={cn(
              'truncate text-[13px] font-medium',
              isPlaying ? 'text-primary' : 'text-foreground',
            )}
            title={entry.title}
          >
            {entry.title}
          </div>
          <p
            className={cn(
              'truncate text-[11px] tabular-nums',
              isPlaying ? 'text-primary/85' : 'text-muted-foreground',
            )}
          >
            {subtitleText ? `${subtitleText} · ${entry.totalFrames} 帧` : `${entry.id} · ${entry.totalFrames} 帧`}
          </p>
        </div>

        {/* Hover: 仅保留预览、管理、删除 3 个空心线框小图标 */}
        <div
          className="flex shrink-0 items-center gap-0.5 opacity-0 transition-opacity group-hover:opacity-100 group-focus-within:opacity-100"
          onClick={(event) => event.stopPropagation()}
          onDoubleClick={(event) => event.stopPropagation()}
        >
          <Button
            type="button"
            variant="ghost"
            size="icon"
            className="size-7 text-muted-foreground hover:bg-accent/70 hover:text-foreground"
            disabled={busy || !canSelect}
            aria-label={`预览 ${entry.title}`}
            title="加载预览"
            onClick={onPreview}
          >
            <RiEyeLine className="size-4" />
          </Button>
          <Button
            type="button"
            variant="ghost"
            size="icon"
            className="size-7 text-muted-foreground hover:bg-accent/70 hover:text-foreground"
            disabled={busy || !canEdit}
            aria-label={`编辑 ${entry.title}`}
            title="管理轨道与同步"
            onClick={onEdit}
          >
            <RiPencilLine className="size-4" />
          </Button>
          <Button
            type="button"
            variant="ghost"
            size="icon"
            className="size-7 text-muted-foreground hover:bg-destructive/12 hover:text-destructive"
            disabled={busy || !canDelete}
            aria-label={`删除 ${entry.title}`}
            title="删除"
            onClick={onDelete}
          >
            <RiDeleteBinLine className="size-4" />
          </Button>
        </div>
      </div>

      {/* 包含内容 */}
      <div role="cell" className="hidden w-[164px] shrink-0 items-center gap-1 sm:flex">
        {tags.length > 0 ? (
          tags.map((tag) => (
            <span
              key={tag}
              className="rounded-[4px] border border-border/75 bg-muted/45 px-1.5 py-0.5 text-[10px] font-medium leading-none text-muted-foreground"
            >
              {tag}
            </span>
          ))
        ) : (
          <span className="text-[11px] text-muted-foreground/60">—</span>
        )}
      </div>

      {/* 时长 */}
      <div role="cell" className="w-16 shrink-0 text-right text-xs tabular-nums text-muted-foreground">
        {formatMmSs(entry.durationSeconds)}
      </div>
    </div>
  )
}

export function LibraryPage({ fork, forkCommand }: { fork?: ForkState | null; forkCommand?: ForkCommand } = {}) {
  const library = useMotionStore((state) => state.library)
  const loaded = useMotionStore((state) => state.loaded)
  const playerState = useMotionStore((state) => state.player.state)
  const selectedKey = useMotionStore((state) => state.selectedLibraryKey)
  const search = useMotionStore((state) => state.librarySearch)
  const filter = useMotionStore((state) => state.libraryFilter)
  const sort = useMotionStore((state) => state.librarySort)
  const pendingDelete = useMotionStore((state) => state.pendingDelete)
  const playlistNotice = useMotionStore((state) => state.playlist.notice)
  const dropNotice = useMotionStore((state) => state.dropNotice)
  const importFormOpen = useMotionStore((state) => state.importForm.open)
  const importUiVisible = useMotionStore((state) => state.importUi.visible)
  const dialogBusy = useMotionStore((state) => state.dialogBusy)
  const injectionAllowed = useMotionStore((state) => state.permission.injectionAllowed)
  const capabilities = useMotionStore((state) => state.capabilities)
  const importBusy = useMotionStore((state) => state.importProgress?.active === true)

  const [moreMenuOpen, setMoreMenuOpen] = React.useState(false)
  const [folder, setFolder] = React.useState('')
  const [folderPath, setFolderPath] = React.useState('')
  const [addingFolder, setAddingFolder] = React.useState(false)
  const [contextMenu, setContextMenu] = React.useState<{
    open: boolean
    x: number
    y: number
    entry: LibraryEntry | null
  }>({ open: false, x: 0, y: 0, entry: null })

  const supports = (method: string) => capabilities.includes(method)
  const busy = library.refreshing || importBusy

  React.useEffect(() => {
    motionStore.startDropWatch()
    return () => motionStore.stopDropWatch()
  }, [capabilities])

  React.useEffect(() => {
    motionStore.syncDropTarget()
  }, [injectionAllowed, importFormOpen, importUiVisible, dialogBusy, busy, pendingDelete])

  const visibleEntries = React.useMemo(() => {
    const needle = search.trim().toLowerCase()
    const filtered = library.entries.filter((entry) => {
      if (folder && entry.folder !== folder) return false
      if (
        needle &&
        !entry.title.toLowerCase().includes(needle) &&
        !entry.id.toLowerCase().includes(needle) &&
        !(entry.subtitle ?? '').toLowerCase().includes(needle)
      ) {
        return false
      }
      if (filter === 'camera') return entry.hasCamera
      if (filter === 'audio') return entry.hasAudio
      if (filter === 'facial') return entry.hasFacial
      if (filter === 'motionOnly') return entry.hasMotion && !entry.hasCamera && !entry.hasAudio && !entry.hasFacial
      return true
    })
    if (sort === 'default') return filtered
    const copy = [...filtered]
    if (sort === 'title') copy.sort((a, b) => a.title.localeCompare(b.title, 'zh-CN'))
    else if (sort === 'durationDesc') copy.sort((a, b) => b.durationSeconds - a.durationSeconds)
    else if (sort === 'durationAsc') copy.sort((a, b) => a.durationSeconds - b.durationSeconds)
    return copy
  }, [library.entries, search, filter, sort, folder])

  // Lightweight windowing for large libraries.
  const scrollRef = React.useRef<HTMLDivElement>(null)
  const [scrollTop, setScrollTop] = React.useState(0)
  const [viewportHeight, setViewportHeight] = React.useState(480)
  React.useEffect(() => {
    const element = scrollRef.current
    if (!element) return
    const observer = new ResizeObserver(() => setViewportHeight(element.clientHeight))
    observer.observe(element)
    setViewportHeight(element.clientHeight)
    return () => observer.disconnect()
  }, [visibleEntries.length])

  const virtualize = visibleEntries.length > VIRTUALIZE_THRESHOLD
  const startIndex = virtualize ? Math.max(0, Math.floor(scrollTop / ROW_HEIGHT) - 5) : 0
  const endIndex = virtualize
    ? Math.min(visibleEntries.length, Math.ceil((scrollTop + viewportHeight) / ROW_HEIGHT) + 5)
    : visibleEntries.length
  const rendered = visibleEntries.slice(startIndex, endIndex)

  const deleteChanged = motionStore.deleteGuardChanged()

  const loadedStatusFor = (entry: LibraryEntry): PlaybackStatus | null => {
    if (loaded.motionKey !== entry.id) return null
    return playerState
  }

  const handlePlayEntry = (entry: LibraryEntry) => {
    if (loaded.motionKey === entry.id && (playerState === 'paused' || playerState === 'preview')) {
      void motionStore.play()
    } else {
      void motionStore.selectMotion(entry.id, true)
    }
  }

  const cycleTitleSort = () => {
    motionStore.setLibrarySort(sort === 'title' ? 'default' : 'title')
  }

  const cycleDurationSort = () => {
    if (sort === 'durationDesc') motionStore.setLibrarySort('durationAsc')
    else if (sort === 'durationAsc') motionStore.setLibrarySort('default')
    else motionStore.setLibrarySort('durationDesc')
  }

  const contextItems = React.useMemo<ContextMenuEntry[]>(() => {
    const entry = contextMenu.entry
    if (!entry) return []
    const isPlaying = loaded.motionKey === entry.id && playerState === 'playing'
    return [
      isPlaying
        ? {
            id: 'pause',
            label: '暂停播放',
            icon: <RiPauseFill className="size-4" />,
            disabled: busy || !supports('player.pause'),
            onSelect: () => void motionStore.pause(),
          }
        : {
            id: 'play',
            label: '播放此曲目',
            icon: <RiPlayFill className="size-4" />,
            disabled: busy || !supports('library.select'),
            onSelect: () => handlePlayEntry(entry),
          },
      {
        id: 'preview',
        label: '加载预览',
        icon: <RiEyeFill className="size-4" />,
        disabled: busy || !supports('library.select'),
        onSelect: () => void motionStore.selectMotion(entry.id, false),
      },
      {
        id: 'edit',
        label: '管理轨道与同步…',
        icon: <RiPencilFill className="size-4" />,
        disabled: busy || !supports('library.startEdit'),
        onSelect: () => void motionStore.openImportEdit(entry.id),
      },
      { type: 'separator', id: 'sep-1' },
      {
        id: 'delete',
        label: '从曲库删除…',
        icon: <RiDeleteBinFill className="size-4" />,
        destructive: true,
        disabled: busy || entry.external === true || !supports('library.delete'),
        onSelect: () => motionStore.requestDelete(entry.id),
      },
    ]
  }, [contextMenu.entry, loaded.motionKey, playerState, busy, capabilities])

  return (
    <div
      className="flex h-full flex-col gap-3"
      data-testid="library-page"
      onClick={() => motionStore.selectLibrary(null)}
    >
      {/* Header Strip: Single primary CTA + icon overflow menu */}
      <div className="flex flex-wrap items-center justify-between gap-2.5" onClick={(event) => event.stopPropagation()}>
        <div className="flex items-center gap-2">
          <h1 className="text-base font-semibold tracking-tight text-foreground">演出曲库</h1>
          <span
            className="rounded-[4px] bg-muted/70 px-1.5 py-0.5 text-[11px] tabular-nums text-muted-foreground"
            data-testid="library-count"
          >
            显示 {visibleEntries.length} / {library.entries.length} 个曲目
          </span>
        </div>

        <div className="flex items-center gap-1.5">
          <Button
            variant="default"
            size="sm"
            className="h-8 text-xs"
            disabled={busy}
            onClick={() => void motionStore.openImportNew([])}
          >
            <RiAddFill className="size-4" />
            添加到曲库
          </Button>

          <Popover open={moreMenuOpen} onOpenChange={setMoreMenuOpen}>
            <PopoverTrigger asChild>
              <Button
                type="button"
                variant="secondary"
                size="icon"
                className="size-8"
                aria-label="更多曲库操作"
                title="刷新、文件夹与重载"
              >
                <RiMoreFill className="size-4" />
              </Button>
            </PopoverTrigger>
            <PopoverContent side="bottom" align="end" className="w-48 p-1">
              <button
                type="button"
                disabled={busy || !supports('library.refresh')}
                onClick={() => {
                  setMoreMenuOpen(false)
                  void motionStore.refreshLibrary()
                }}
                className="flex w-full items-center gap-2 rounded px-2.5 py-1.5 text-left text-xs text-foreground transition-colors hover:bg-accent disabled:pointer-events-none disabled:opacity-45"
              >
                <RiRefreshFill className={cn('size-3.5 text-muted-foreground', library.refreshing && 'animate-spin')} />
                <span>{library.refreshing ? '正在刷新...' : '刷新曲库'}</span>
              </button>
              <button
                type="button"
                disabled={!supports('library.openFolder')}
                onClick={() => {
                  setMoreMenuOpen(false)
                  void motionStore.openLibraryFolder()
                }}
                className="flex w-full items-center gap-2 rounded px-2.5 py-1.5 text-left text-xs text-foreground transition-colors hover:bg-accent disabled:pointer-events-none disabled:opacity-45"
              >
                <RiFolderOpenFill className="size-3.5 text-muted-foreground" />
                <span>打开曲库文件夹</span>
              </button>
              <button
                type="button"
                disabled={loaded.motionKey === '' || loaded.sourceMissing || busy || !supports('player.reload')}
                onClick={() => {
                  setMoreMenuOpen(false)
                  void motionStore.reloadMotion()
                }}
                className="flex w-full items-center gap-2 rounded px-2.5 py-1.5 text-left text-xs text-foreground transition-colors hover:bg-accent disabled:pointer-events-none disabled:opacity-45"
              >
                <RiRefreshFill className="size-3.5 text-muted-foreground" />
                <span>重新加载当前曲目</span>
              </button>
            </PopoverContent>
          </Popover>

          {!supports('library.import') && <PendingBadge />}
        </div>
      </div>

      {/* Notices */}
      {(dropNotice || library.notice || (playlistNotice && playlistNotice !== library.notice) || loaded.sourceMissing || loaded.sourceChanged) && (
        <div className="space-y-1" onClick={(event) => event.stopPropagation()}>
          {dropNotice && (
            <p className="rounded-md border border-warning/35 bg-warning/10 px-2.5 py-1.5 text-xs text-warning" data-testid="library-drop-notice">
              {dropNotice}
            </p>
          )}
          {library.notice && (
            <p className="rounded-md bg-card px-2.5 py-1.5 text-xs text-muted-foreground">{library.notice}</p>
          )}
          {playlistNotice && playlistNotice !== library.notice && (
            <p
              className={cn(
                'rounded-md border px-2.5 py-1.5 text-xs',
                /失败|错误/.test(playlistNotice)
                  ? 'border-destructive/40 bg-destructive/10 text-destructive'
                  : 'border-primary/30 bg-primary/10 text-primary',
              )}
            >
              {playlistNotice}
            </p>
          )}
          {loaded.sourceMissing && (
            <p className="rounded-md border border-warning/40 bg-warning/10 px-2.5 py-1.5 text-xs text-warning">
              当前表演的源文件已移除；已加载内容保留，无法重新加载。
            </p>
          )}
          {!loaded.sourceMissing && loaded.sourceChanged && (
            <p className="rounded-md border border-warning/40 bg-warning/10 px-2.5 py-1.5 text-xs text-warning">
              当前曲目的文件已更新，待重新加载。
            </p>
          )}
        </div>
      )}

      {/* Search + Funnel Dropdown Filter Toolbar */}
      <div className="flex items-center gap-2" onClick={(event) => event.stopPropagation()}>
        <div className="relative min-w-[200px] flex-1 sm:max-w-[260px]">
          <RiSearchLine className="pointer-events-none absolute left-2.5 top-1/2 size-3.5 -translate-y-1/2 text-muted-foreground" />
          <Input
            className="h-8 pl-8 pr-12 text-xs rounded-full"
            placeholder="搜索..."
            aria-label="搜索曲目"
            value={search}
            onChange={(event) => motionStore.setLibrarySearch(event.target.value)}
          />
          {search !== '' && (
            <Button
              type="button"
              variant="ghost"
              size="sm"
              className="absolute right-1 top-1/2 h-6 -translate-y-1/2 px-1.5 text-[11px]"
              onClick={() => motionStore.setLibrarySearch('')}
            >
              清空
            </Button>
          )}
        </div>

        <DropdownMenu>
          <DropdownMenuTrigger asChild>
            <Button
              type="button"
              variant={filter !== 'all' ? 'secondary' : 'ghost'}
              size="icon"
              className={cn('relative size-8', filter !== 'all' ? 'text-primary' : 'text-muted-foreground hover:text-foreground')}
              aria-label="轨道筛选"
              title="轨道筛选"
            >
              <RiFilter2Fill className="size-4" />
              {filter !== 'all' && (
                <span aria-hidden="true" className="absolute right-1.5 top-1.5 size-1.5 rounded-full bg-primary" />
              )}
            </Button>
          </DropdownMenuTrigger>
          <DropdownMenuContent align="end">
            <DropdownMenuLabel>按轨道筛选</DropdownMenuLabel>
            <DropdownMenuSeparator />
            {FILTER_OPTIONS.map((opt) => (
              <DropdownMenuCheckboxItem
                key={opt.value}
                checked={filter === opt.value}
                onCheckedChange={() =>
                  motionStore.setLibraryFilter(filter === opt.value && opt.value !== 'all' ? 'all' : opt.value)
                }
              >
                {opt.label}
              </DropdownMenuCheckboxItem>
            ))}
          </DropdownMenuContent>
        </DropdownMenu>
      </div>

      {/* Implicit Borderless List (horizontal padding prevents left/right card shadow clipping) */}
      {fork && forkCommand && (
        <div className="flex flex-wrap items-center gap-2 border-y py-2" onClick={event => event.stopPropagation()}>
          <label className="flex items-center gap-2 text-xs text-muted-foreground">文件夹
            <select className="h-8 max-w-64 rounded-md border bg-background px-2 text-foreground" value={folder}
              onChange={event => setFolder(event.currentTarget.value)}>
              <option value="">所有文件夹</option>
              {fork.folders.map(path => <option key={path} value={path}>{path.split(/[\\/]/).filter(Boolean).at(-1) ?? path}</option>)}
            </select>
          </label>
          <Button size="sm" variant="outline" onClick={() => setAddingFolder(!addingFolder)}>添加文件夹</Button>
          {folder && <Button size="sm" variant="ghost" onClick={() => { void forkCommand('folder.remove', { path: folder }); setFolder('') }}>移除文件夹</Button>}
          {addingFolder && <form className="flex gap-2" onSubmit={event => { event.preventDefault(); void forkCommand('folder.add', { path: folderPath.trim() }); setAddingFolder(false) }}>
            <input className="w-55 min-w-32 rounded-md border bg-background px-2 text-xs" aria-label="文件夹路径" placeholder="输入完整文件夹路径"
              value={folderPath} onChange={event => setFolderPath(event.currentTarget.value)} />
            <Button size="sm" type="submit" disabled={!folderPath.trim()}>扫描</Button>
          </form>}
          {fork.folderNotice && <span className="text-xs text-muted-foreground">{fork.folderNotice}</span>}
        </div>
      )}
      {library.entries.length === 0 ? (
        <div
          className="flex flex-1 flex-col items-center justify-center gap-3 rounded-lg border border-dashed bg-card/40 p-8 text-center"
          onClick={(event) => event.stopPropagation()}
        >
          <p className="text-sm font-medium text-foreground">曲库为空</p>
          <p className="max-w-sm text-xs text-muted-foreground">
            点击“添加到曲库”或直接将 VMD / GKCamera / WAV 文件拖入窗口；也可将动作文件夹放入曲库后点击右上角菜单刷新。
          </p>
          <Button size="sm" disabled={busy} onClick={() => void motionStore.openImportNew([])}>
            <RiAddFill className="size-4" />
            导入首个演出
          </Button>
        </div>
      ) : visibleEntries.length === 0 ? (
        <div
          className="flex flex-1 flex-col items-center justify-center gap-2 rounded-lg border border-dashed bg-card/40 p-8 text-center"
          onClick={(event) => event.stopPropagation()}
        >
          <p className="text-sm text-muted-foreground">没有匹配的演出曲目</p>
          <Button
            variant="outline"
            size="sm"
            onClick={() => {
              motionStore.setLibrarySearch('')
              motionStore.setLibraryFilter('all')
            }}
          >
            重置搜索与筛选
          </Button>
        </div>
      ) : (
        <div
          ref={scrollRef}
          role="table"
          aria-label="演出曲库列表"
          className="-mx-2 min-h-0 flex-1 overflow-y-auto px-2"
          onScroll={(event) => setScrollTop(event.currentTarget.scrollTop)}
          onClick={() => motionStore.selectLibrary(null)}
          data-testid="library-table-scroll"
        >
          {/* Minimal Implicit Header */}
          <div
            role="row"
            onClick={(event) => event.stopPropagation()}
            className="sticky top-0 z-10 flex items-center gap-3 bg-background/95 px-3 py-1.5 text-[11px] font-medium text-muted-foreground backdrop-blur-sm"
          >
            <div role="columnheader" className="w-8 shrink-0 text-center">
              #
            </div>
            <div role="columnheader" className="min-w-0 flex-1">
              <button
                type="button"
                onClick={cycleTitleSort}
                className="inline-flex items-center gap-1 rounded px-1 py-0.5 text-[11px] font-medium text-muted-foreground transition-colors hover:text-foreground"
              >
                <span>演出曲目</span>
                {sort === 'title' ? (
                  <RiArrowUpFill className="size-3.5 text-primary" />
                ) : (
                  <RiArrowUpDownFill className="size-3.5 opacity-55" />
                )}
              </button>
            </div>
            <div role="columnheader" className="hidden w-[164px] shrink-0 sm:block">
              包含内容
            </div>
            <div role="columnheader" className="w-16 shrink-0 text-right">
              <button
                type="button"
                onClick={cycleDurationSort}
                className="inline-flex items-center justify-end gap-1 rounded px-1 py-0.5 text-[11px] font-medium text-muted-foreground transition-colors hover:text-foreground"
              >
                <span>时长</span>
                {sort === 'durationDesc' ? (
                  <RiArrowDownFill className="size-3.5 text-primary" />
                ) : sort === 'durationAsc' ? (
                  <RiArrowUpFill className="size-3.5 text-primary" />
                ) : (
                  <RiArrowUpDownFill className="size-3.5 opacity-55" />
                )}
              </button>
            </div>
          </div>

          <div role="rowgroup" className="space-y-1 py-1.5">
            {virtualize && startIndex > 0 && <div aria-hidden style={{ height: startIndex * ROW_HEIGHT }} />}
            {rendered.map((entry, idx) => (
              <LibraryRow
                key={entry.id}
                entry={entry}
                index={startIndex + idx}
                selected={selectedKey === entry.id}
                loadedStatus={loadedStatusFor(entry)}
                busy={busy}
                canSelect={supports('library.select')}
                canPause={supports('player.pause')}
                canEdit={supports('library.startEdit')}
                canDelete={supports('library.delete') && !entry.external}
                style={virtualize ? { height: ROW_HEIGHT } : undefined}
                onSelect={() => motionStore.selectLibrary(entry.id)}
                onDoubleClick={() => void motionStore.selectMotion(entry.id, true)}
                onContextMenu={(event) => {
                  event.preventDefault()
                  motionStore.selectLibrary(entry.id)
                  setContextMenu({ open: true, x: event.clientX, y: event.clientY, entry })
                }}
                onPlay={() => handlePlayEntry(entry)}
                onPause={() => void motionStore.pause()}
                onPreview={() => void motionStore.selectMotion(entry.id, false)}
                onEdit={() => void motionStore.openImportEdit(entry.id)}
                onDelete={() => motionStore.requestDelete(entry.id)}
              />
            ))}
            {virtualize && endIndex < visibleEntries.length && (
              <div aria-hidden style={{ height: (visibleEntries.length - endIndex) * ROW_HEIGHT }} />
            )}
          </div>
        </div>
      )}

      {/* Row Right-Click Context Menu */}
      <ContextMenu
        open={contextMenu.open}
        x={contextMenu.x}
        y={contextMenu.y}
        items={contextItems}
        onClose={() => setContextMenu((prev) => ({ ...prev, open: false }))}
        testId="library-context-menu"
      />

      {/* Single Delete Confirmation */}
      <AlertDialog
        open={pendingDelete !== null}
        onOpenChange={(open) => {
          if (!open) motionStore.cancelDelete()
        }}
      >
        <AlertDialogContent data-testid="delete-dialog" className="max-h-[calc(100dvh_-_2rem)] w-[calc(100%_-_2rem)] grid-cols-1 overflow-y-auto">
          <AlertDialogHeader>
            <AlertDialogTitle>删除选中确认</AlertDialogTitle>
            <AlertDialogDescription asChild>
              <div className="min-w-0 space-y-2 [overflow-wrap:anywhere]">
                <p>删除曲目：{pendingDelete?.key}</p>
                <p>将永久删除整个曲目目录及其中全部文件：{pendingDelete?.directory}</p>
                <p>此操作无法撤销，文件不会进入回收站。确定删除选中曲目？</p>
                {deleteChanged && <p className="text-warning">曲库已变化，请取消后重新选择曲目。</p>}
              </div>
            </AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter className="min-w-0 flex-wrap">
            <AlertDialogCancel onClick={() => motionStore.cancelDelete()}>取消删除</AlertDialogCancel>
            <AlertDialogAction
              disabled={deleteChanged || busy}
              onClick={(event) => {
                event.preventDefault()
                void motionStore.confirmDelete()
              }}
            >
              永久删除
            </AlertDialogAction>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialog>

      <ImportDialog />
    </div>
  )
}
