import * as React from 'react'
import { FolderOpen, Pencil, Plus, RefreshCw, Trash2 } from 'lucide-react'
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
import { Input } from '@/components/ui/input'
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from '@/components/ui/table'
import { cn } from '@/lib/utils'
import { motionStore, useMotionStore } from '../store'
import type { LibraryEntry } from '../protocol'
import { formatMmSs } from '../time'
import { PendingBadge } from '../components/Unsupported'
import { ImportDialog } from './ImportDialog'

const ROW_HEIGHT = 36
const VIRTUALIZE_THRESHOLD = 200

function StatusBadge({ entry }: { entry: LibraryEntry }) {
  const loadedKey = useMotionStore((state) => state.loaded.motionKey)
  const playerState = useMotionStore((state) => state.player.state)
  if (loadedKey !== entry.id) return null
  if (playerState === 'playing') return <span className="text-success">[播]</span>
  if (playerState === 'paused') return <span className="text-warning">[暂]</span>
  if (playerState === 'preview') return <span className="text-primary">[预]</span>
  return <span className="text-muted-foreground">[载]</span>
}

function ContentTags({ entry }: { entry: LibraryEntry }) {
  return (
    <span className="space-x-1 text-xs">
      {entry.hasMotion && <span className="text-primary">[动作]</span>}
      {entry.hasFacial && <span className="text-success">[表情]</span>}
      {entry.hasCamera && <span className="text-warning">[镜头]</span>}
      {entry.hasAudio && <span className="text-destructive">[音乐]</span>}
    </span>
  )
}

export function LibraryPage() {
  const library = useMotionStore((state) => state.library)
  const loaded = useMotionStore((state) => state.loaded)
  const selectedKey = useMotionStore((state) => state.selectedLibraryKey)
  const search = useMotionStore((state) => state.librarySearch)
  const pendingDelete = useMotionStore((state) => state.pendingDelete)
  const playlistNotice = useMotionStore((state) => state.playlist.notice)
  const dropNotice = useMotionStore((state) => state.dropNotice)
  const importFormOpen = useMotionStore((state) => state.importForm.open)
  const importUiVisible = useMotionStore((state) => state.importUi.visible)
  const dialogBusy = useMotionStore((state) => state.dialogBusy)
  const injectionAllowed = useMotionStore((state) => state.permission.injectionAllowed)
  const capabilities = useMotionStore((state) => state.capabilities)
  const importBusy = useMotionStore((state) => state.importProgress?.active === true)

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
    if (!needle) return library.entries
    return library.entries.filter(
      (entry) => entry.title.toLowerCase().includes(needle) || entry.id.toLowerCase().includes(needle),
    )
  }, [library.entries, search])

  const selectedEntry = library.entries.find((entry) => entry.id === selectedKey) ?? null

  // Lightweight windowing for large libraries (ImGui ListClipper equivalent).
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
  }, [])
  const virtualize = visibleEntries.length > VIRTUALIZE_THRESHOLD
  const startIndex = virtualize ? Math.max(0, Math.floor(scrollTop / ROW_HEIGHT) - 5) : 0
  const endIndex = virtualize
    ? Math.min(visibleEntries.length, Math.ceil((scrollTop + viewportHeight) / ROW_HEIGHT) + 5)
    : visibleEntries.length
  const rendered = visibleEntries.slice(startIndex, endIndex)

  const deleteChanged = motionStore.deleteGuardChanged()

  const handleRowClick = (entry: LibraryEntry) => {
    motionStore.selectLibrary(entry.id)
  }
  const handleRowDoubleClick = (entry: LibraryEntry) => {
    if (busy || !supports('library.select')) return
    motionStore.selectLibrary(entry.id)
    void motionStore.selectMotion(entry.id, true)
  }

  return (
    <div className="flex h-full flex-col gap-3" data-testid="library-page">
      {/* Toolbar */}
      <div className="flex flex-wrap items-center gap-2">
        <Button
          variant="outline"
          size="sm"
          disabled={busy || !supports('library.refresh')}
          onClick={() => void motionStore.refreshLibrary()}
        >
          <RefreshCw className={cn('size-4', library.refreshing && 'animate-spin')} />
          {library.refreshing ? '正在刷新...' : '刷新曲库'}
        </Button>
        <Button
          variant="outline"
          size="sm"
          disabled={!supports('library.openFolder')}
          onClick={() => void motionStore.openLibraryFolder()}
        >
          <FolderOpen className="size-4" />
          打开曲库文件夹
        </Button>
        <Button
          variant="outline"
          size="sm"
          disabled={loaded.motionKey === '' || loaded.sourceMissing || busy || !supports('player.reload')}
          title="载入当前曲目的磁盘更新；保留时间并暂停，点击继续后播放"
          onClick={() => void motionStore.reloadMotion()}
        >
          <RefreshCw className="size-4" />
          重新加载当前曲目
        </Button>
        <Button variant="outline" size="sm" disabled={busy} onClick={() => void motionStore.openImportNew([])}>
          <Plus className="size-4" />
          添加到曲库
        </Button>
        {!supports('library.import') && <PendingBadge />}
        <Button
          variant="outline"
          size="sm"
          disabled={!selectedEntry || busy || !supports('library.startEdit')}
          onClick={() => selectedEntry && void motionStore.openImportEdit(selectedEntry.id)}
        >
          <Pencil className="size-4" />
          管理选中
        </Button>
        <Button
          variant="outline"
          size="sm"
          disabled={!selectedEntry || busy || !supports('library.delete')}
          onClick={() => selectedEntry && motionStore.requestDelete(selectedEntry.id)}
        >
          <Trash2 className="size-4" />
          删除选中
        </Button>
      </div>

      {/* Notices */}
      {dropNotice && (
        <p className="text-sm text-warning" data-testid="library-drop-notice">
          {dropNotice}
        </p>
      )}
      {library.notice && <p className="text-sm text-muted-foreground">{library.notice}</p>}
      {playlistNotice && playlistNotice !== library.notice && (
        <p className={cn('text-sm', /失败|错误/.test(playlistNotice) ? 'text-destructive' : 'text-primary')}>
          {playlistNotice}
        </p>
      )}
      {loaded.sourceMissing && (
        <p className="text-sm text-warning">当前表演的源文件已移除；已加载内容保留，无法重新加载。</p>
      )}
      {!loaded.sourceMissing && loaded.sourceChanged && (
        <p className="text-sm text-warning">当前曲目的文件已更新，待重新加载。</p>
      )}

      {/* Search */}
      <div className="flex items-center gap-2">
        <Input
          className="h-8 max-w-sm"
          placeholder="搜索曲目"
          aria-label="搜索曲目"
          value={search}
          onChange={(event) => motionStore.setLibrarySearch(event.target.value)}
        />
        {search !== '' && (
          <Button variant="ghost" size="sm" onClick={() => motionStore.setLibrarySearch('')}>
            清空
          </Button>
        )}
      </div>

      {/* Selection actions */}
      {selectedEntry && (
        <div className="flex flex-wrap items-center gap-2 text-sm">
          <span className="text-muted-foreground">选中：{selectedEntry.title}</span>
          <Button
            variant="outline"
            size="sm"
            disabled={busy || !supports('library.select')}
            onClick={() => void motionStore.selectMotion(selectedEntry.id, false)}
          >
            加载预览
          </Button>
          <Button
            variant="outline"
            size="sm"
            disabled={busy || !supports('library.select')}
            onClick={() => void motionStore.selectMotion(selectedEntry.id, true)}
          >
            播放选中
          </Button>
        </div>
      )}

      <div className="flex items-center justify-between text-xs text-muted-foreground">
        <span data-testid="library-count">
          显示 {visibleEntries.length} / {library.entries.length} 个曲目
        </span>
        <span className="flex items-center gap-1.5">
          将 VMD / GKCamera / WAV 文件拖入本窗口任意位置，即可添加到曲库
          {!supports('drop.poll') && <PendingBadge />}
        </span>
      </div>

      {/* Table */}
      {library.entries.length === 0 ? (
        <p className="text-sm text-muted-foreground">曲库为空。添加动作，或将动作文件夹放入曲库后点击“刷新曲库”。</p>
      ) : visibleEntries.length === 0 ? (
        <p className="text-sm text-muted-foreground">没有匹配的曲目，可清空搜索后查看全部曲目。</p>
      ) : (
        <div
          ref={scrollRef}
          className="min-h-0 flex-1 overflow-y-auto rounded-md border"
          onScroll={(event) => setScrollTop(event.currentTarget.scrollTop)}
          data-testid="library-table-scroll"
        >
          <Table>
            <TableHeader className="sticky top-0 z-10 bg-card">
              <TableRow>
                <TableHead className="w-[52px]">状态</TableHead>
                <TableHead>演出曲目</TableHead>
                <TableHead className="w-[160px]">包含内容</TableHead>
                <TableHead className="w-[70px]">时长</TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              {virtualize && startIndex > 0 && (
                <TableRow aria-hidden style={{ height: startIndex * ROW_HEIGHT }}>
                  <TableCell colSpan={4} className="p-0" />
                </TableRow>
              )}
              {rendered.map((entry) => (
                <TableRow
                  key={entry.id}
                  data-state={selectedKey === entry.id ? 'selected' : undefined}
                  data-testid={`library-row-${entry.id}`}
                  className={cn('cursor-pointer', selectedKey === entry.id && 'bg-accent/60')}
                  style={virtualize ? { height: ROW_HEIGHT } : undefined}
                  onClick={() => handleRowClick(entry)}
                  onDoubleClick={() => handleRowDoubleClick(entry)}
                >
                  <TableCell className="py-1.5">
                    <StatusBadge entry={entry} />
                  </TableCell>
                  <TableCell className="py-1.5">
                    <span className={cn(loaded.motionKey === entry.id && 'font-medium')}>{entry.title}</span>
                  </TableCell>
                  <TableCell className="py-1.5">
                    <ContentTags entry={entry} />
                  </TableCell>
                  <TableCell className="py-1.5 text-muted-foreground tabular-nums">
                    {formatMmSs(entry.durationSeconds)}
                  </TableCell>
                </TableRow>
              ))}
              {virtualize && endIndex < visibleEntries.length && (
                <TableRow aria-hidden style={{ height: (visibleEntries.length - endIndex) * ROW_HEIGHT }}>
                  <TableCell colSpan={4} className="p-0" />
                </TableRow>
              )}
            </TableBody>
          </Table>
        </div>
      )}

      {/* Delete confirmation with libraryRevision guard */}
      <AlertDialog
        open={pendingDelete !== null}
        onOpenChange={(open) => {
          if (!open) motionStore.cancelDelete()
        }}
      >
        <AlertDialogContent data-testid="delete-dialog">
          <AlertDialogHeader>
            <AlertDialogTitle>删除选中确认</AlertDialogTitle>
            <AlertDialogDescription asChild>
              <div className="space-y-2">
                <p>删除曲目：{pendingDelete?.key}</p>
                <p>将永久删除整个曲目目录及其中全部文件：{pendingDelete?.directory}</p>
                <p>此操作无法撤销，文件不会进入回收站。确定删除选中曲目？</p>
                {deleteChanged && <p className="text-warning">曲库已变化，请取消后重新选择曲目。</p>}
              </div>
            </AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter>
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
