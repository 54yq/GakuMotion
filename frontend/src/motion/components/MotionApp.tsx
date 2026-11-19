import * as React from 'react'
import {
  RiFlaskFill,
  RiLoader4Fill,
  RiPlugFill,
  RiRefreshFill,
  RiUploadCloud2Fill,
  RiWindowFill,
} from '@remixicon/react'
import { Alert, AlertDescription } from '@/components/ui/alert'
import { Button } from '@/components/ui/button'
import { LiveControls, useForkWorkspace, type ForkCommand } from './ForkWorkspace'
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuLabel,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from '@/components/ui/dropdown-menu'
import { TooltipProvider } from '@/components/ui/tooltip'
import { motionStore, useMotionStore } from '../store'
import { getMotionBridge, MotionMockBridgeClient, setMotionBridge } from '../bridge'
import { useMotionAppearanceEffect, useMotionKeyboardShortcuts, useMotionTextInputBridge } from '../hooks'
import { DropOverlay } from './DropOverlay'
import { ImportProgressModal } from './ImportProgressModal'
import { MotionErrorBoundary } from './MotionErrorBoundary'
import { MotionSidebar } from './MotionSidebar'
import { MotionStatusBar } from './MotionStatusBar'
import { PermissionGate } from './PermissionGate'
import { TransportBar } from './TransportBar'
import { LibraryPage } from '../pages/LibraryPage'
import { MotionPage } from '../pages/MotionPage'
import { CameraPage } from '../pages/CameraPage'
import { StagePage } from '../pages/StagePage'
import { SettingsPage } from '../pages/SettingsPage'
import { DeveloperPage } from '../pages/DeveloperPage'

function BootScreen({ error, canReconnect }: { error: string | null; canReconnect: boolean }) {
  return (
    <div className="flex h-full flex-col items-center justify-center gap-4 p-6 text-muted-foreground">
      {error ? (
        <div className="flex max-w-md flex-col items-center gap-3 rounded-xl border bg-card/85 p-6 text-center shadow-sm">
          <div className="flex size-11 items-center justify-center rounded-full bg-destructive/15 text-destructive">
            <RiPlugFill className="size-5" />
          </div>
          <div className="space-y-1">
            <p className="text-sm font-medium text-foreground">GakuMotion 原生桥接不可用</p>
            <p className="text-xs leading-relaxed text-muted-foreground">{error}</p>
          </div>
          {canReconnect && (
            <Button type="button" variant="outline" size="sm" onClick={() => void motionStore.restart()}>
              <RiRefreshFill className="size-3.5" />
              重新连接
            </Button>
          )}
        </div>
      ) : (
        <div className="flex flex-col items-center gap-2.5">
          <RiLoader4Fill className="size-7 animate-spin text-primary" />
          <p className="text-sm">正在连接 GakuMotion…</p>
        </div>
      )}
    </div>
  )
}

function ActivePage({ fork, command }: { fork: ReturnType<typeof useForkWorkspace>['state']; command: ForkCommand }) {
  const page = useMotionStore((state) => state.page)
  switch (page) {
    case 'library':
      return <LibraryPage fork={fork} forkCommand={command} />
    case 'motion':
      return <MotionPage fork={fork} forkCommand={command} />
    case 'camera':
      return <CameraPage />
    case 'stage':
      return <StagePage />
    case 'settings':
      return <SettingsPage />
    case 'developer':
      return <DeveloperPage />
  }
}

function isStandalonePreview(): boolean {
  if (import.meta.env.MODE === 'preview') return true
  if (typeof window === 'undefined') return false
  return new URLSearchParams(window.location.search).get('preview') === '1'
}

const PREVIEW_THEMES = [
  { value: 0, label: 'Dark' },
  { value: 1, label: 'Sapphire' },
  { value: 2, label: 'Paper & Ink' },
  { value: 3, label: 'Apple Light' },
  { value: 4, label: 'Apple Dark' },
]

const PREVIEW_SCALES = [0.75, 1, 1.25, 1.5]

function PreviewFontStats({ appearance }: { appearance: { theme: number; uiScale: number } }) {
  const [stats, setStats] = React.useState({ rootPx: 16, bodyPx: 14, dpr: 1 })

  React.useEffect(() => {
    const measure = () => {
      const rootPx = Number.parseFloat(window.getComputedStyle(document.documentElement).fontSize) || 16
      const bodyPx = Number.parseFloat(window.getComputedStyle(document.body).fontSize) || rootPx
      const dpr = window.devicePixelRatio || 1
      setStats({ rootPx, bodyPx, dpr })
    }
    const raf = window.requestAnimationFrame(measure)
    window.addEventListener('resize', measure)
    return () => {
      window.cancelAnimationFrame(raf)
      window.removeEventListener('resize', measure)
    }
  }, [appearance.theme, appearance.uiScale])

  const effectivePx = stats.bodyPx * appearance.uiScale
  return (
    <span
      className="inline-flex h-6 items-center rounded border bg-background/80 px-2 font-mono text-[11px] text-muted-foreground"
      data-testid="preview-font-stats"
      title={`html: ${stats.rootPx.toFixed(1)}px | body: ${stats.bodyPx.toFixed(1)}px | zoom: ${Math.round(appearance.uiScale * 100)}% | DPR: ${stats.dpr.toFixed(2)}`}
    >
      字号: 根{stats.rootPx.toFixed(0)} / 正文{stats.bodyPx.toFixed(0)}px (等效{effectivePx.toFixed(1)}px)
    </span>
  )
}

function PreviewToolbar({
  appearance,
  injectionAllowed,
}: {
  appearance: { theme: number; uiScale: number }
  injectionAllowed: boolean
}) {
  const client = getMotionBridge()
  const mock = client instanceof MotionMockBridgeClient ? client : null
  const [profile, setProfile] = React.useState<'full' | 'phase1'>(() =>
    mock?.getCapabilityProfile() ?? 'full',
  )
  const entries = useMotionStore((state) => state.library.entries)
  if (!mock) return null
  return (
    <div className="ml-auto flex flex-wrap items-center gap-2" data-testid="preview-toolbar">
      <PreviewFontStats appearance={appearance} />
      <label className="flex items-center gap-1 text-xs">
        <span className="text-muted-foreground">主题</span>
        <select
          aria-label="预览主题"
          className="h-6 rounded border bg-background px-1.5 text-xs text-foreground"
          value={appearance.theme}
          onChange={(e) => mock.setAppearance(Number(e.target.value), appearance.uiScale)}
        >
          {PREVIEW_THEMES.map((t) => (
            <option key={t.value} value={t.value}>
              {t.label}
            </option>
          ))}
        </select>
      </label>
      <label className="flex items-center gap-1 text-xs">
        <span className="text-muted-foreground">缩放</span>
        <select
          aria-label="预览缩放"
          className="h-6 rounded border bg-background px-1.5 text-xs text-foreground"
          value={appearance.uiScale}
          onChange={(e) => mock.setAppearance(appearance.theme, Number(e.target.value))}
        >
          {PREVIEW_SCALES.map((s) => (
            <option key={s} value={s}>
              {Math.round(s * 100)}%
            </option>
          ))}
        </select>
      </label>
      <label className="flex items-center gap-1 text-xs">
        <span className="text-muted-foreground">能力集</span>
        <select
          aria-label="模拟能力集"
          className="h-6 rounded border bg-background px-1.5 text-xs text-foreground"
          value={profile}
          onChange={(e) => {
            const next = e.target.value === 'phase1' ? 'phase1' : 'full'
            setProfile(next)
            mock.setCapabilityProfile(next)
            void motionStore.resync()
          }}
        >
          <option value="full">完整预览</option>
          <option value="phase1">基础降级</option>
        </select>
      </label>
      <DropdownMenu>
        <DropdownMenuTrigger asChild>
          <Button
            type="button"
            variant="outline"
            size="sm"
            className="h-6 px-2 text-xs"
            data-testid="preview-special-modals-trigger"
          >
            <RiWindowFill className="size-3" />
            特殊弹窗
          </Button>
        </DropdownMenuTrigger>
        <DropdownMenuContent align="end" className="w-64">
          <DropdownMenuLabel>导入流程弹窗（Issue #5）</DropdownMenuLabel>
          <DropdownMenuItem onClick={() => motionStore.previewImportFlow('success')}>
            导入中 → 导入成功弹窗
          </DropdownMenuItem>
          <DropdownMenuItem onClick={() => motionStore.previewImportFlow('error')}>
            导入中 → 导入出错弹窗 (IOException)
          </DropdownMenuItem>
          <DropdownMenuItem onClick={() => motionStore.previewImportErrorDirect()}>
            直接打开导入出错弹窗
          </DropdownMenuItem>
          <DropdownMenuItem
            disabled={!injectionAllowed || entries.length === 0}
            onClick={() => {
              const target = entries[0]
              if (!target) return
              motionStore.setPage('library')
              void (async () => {
                await motionStore.selectMotion(target.id, true)
                await motionStore.openImportEdit(target.id)
              })()
            }}
          >
            编辑播放中曲目（停止并保存保护）
          </DropdownMenuItem>
          <DropdownMenuSeparator />
          <DropdownMenuLabel>曲库与删除确认</DropdownMenuLabel>
          <DropdownMenuItem
            disabled={!injectionAllowed || entries.length === 0}
            onClick={() => motionStore.previewDeleteDialog(false)}
          >
            删除确认弹窗
          </DropdownMenuItem>
          <DropdownMenuItem
            disabled={!injectionAllowed || entries.length === 0}
            onClick={() => motionStore.previewDeleteDialog(true)}
          >
            曲库过期冲突删除弹窗
          </DropdownMenuItem>
          <DropdownMenuSeparator />
          <DropdownMenuLabel>拖放遮罩（DropOverlay）</DropdownMenuLabel>
          <DropdownMenuItem
            disabled={!injectionAllowed}
            onClick={() => {
              motionStore.setPage('library')
              motionStore.previewDropOverlay({
                visual: 'hover',
                fileCount: 3,
                accepted: 3,
                hostElevated: false,
                hostBlocked: false,
              })
            }}
          >
            拖放悬停遮罩（有效文件）
          </DropdownMenuItem>
          <DropdownMenuItem
            disabled={!injectionAllowed}
            onClick={() => {
              motionStore.setPage('library')
              motionStore.previewDropOverlay({
                visual: 'hover',
                fileCount: 2,
                accepted: 0,
                hostElevated: true,
                hostBlocked: false,
              })
            }}
          >
            拖放遮罩（管理员提权提示）
          </DropdownMenuItem>
          <DropdownMenuItem
            disabled={!injectionAllowed}
            onClick={() => {
              motionStore.setPage('library')
              motionStore.previewDropOverlay({
                visual: 'invalid',
                fileCount: 2,
                accepted: 0,
                hostElevated: false,
                hostBlocked: false,
              })
            }}
          >
            拖放遮罩（无效文件）
          </DropdownMenuItem>
          <DropdownMenuItem
            onClick={() =>
              motionStore.previewDropOverlay({
                visual: 'none',
                fileCount: 0,
                accepted: 0,
                hostElevated: false,
                hostBlocked: false,
              })
            }
          >
            关闭拖放遮罩
          </DropdownMenuItem>
          <DropdownMenuSeparator />
          <DropdownMenuLabel>系统异常状态</DropdownMenuLabel>
          <DropdownMenuItem
            onClick={() =>
              motionStore.previewActionError(
                '播放中禁止覆盖当前曲目：请先停止播放后再保存（Issue #5 IOException 防护）',
              )
            }
          >
            全局操作错误横幅
          </DropdownMenuItem>
          <DropdownMenuItem onClick={() => motionStore.previewDisconnect()}>
            原生桥接断连重连页
          </DropdownMenuItem>
        </DropdownMenuContent>
      </DropdownMenu>
      <Button
        type="button"
        variant="outline"
        size="sm"
        className="h-6 px-2 text-xs"
        disabled={!injectionAllowed || profile !== 'full'}
        onClick={() => {
          motionStore.setPage('library')
          mock.simulateFileDrop([
            'C:\\Preview\\DemoLive_motion.vmd',
            'C:\\Preview\\DemoLive_camera.vmd',
            'C:\\Preview\\DemoLive_song.wav',
          ])
        }}
      >
        <RiUploadCloud2Fill className="size-3" />
        模拟拖入
      </Button>
      <Button
        type="button"
        variant="outline"
        size="sm"
        className="h-6 px-2 text-xs"
        onClick={() =>
          mock.setPermission(!injectionAllowed, injectionAllowed ? '独立预览：模拟控制已禁用' : '')
        }
      >
        {injectionAllowed ? '模拟禁用控制' : '恢复允许控制'}
      </Button>
      <Button
        type="button"
        variant="outline"
        size="sm"
        className="h-6 px-2 text-xs"
        onClick={() => {
          mock.close()
          setMotionBridge(null)
          motionStore.reset()
          void motionStore.start()
        }}
      >
        <RiRefreshFill className="size-3" />
        重置会话
      </Button>
    </div>
  )
}

export default function MotionApp() {
  const ready = useMotionStore((state) => state.ready)
  const connected = useMotionStore((state) => state.connected)
  const error = useMotionStore((state) => state.error)
  const bridgeKind = useMotionStore((state) => state.bridgeKind)
  const permission = useMotionStore((state) => state.permission)
  const appearance = useMotionStore((state) => state.appearance)
  const page = useMotionStore((state) => state.page)
  const textInputSupported = useMotionStore((state) => state.capabilities.includes('shell.setTextInputActive'))
  const standalonePreview = isStandalonePreview()
  const fork = useForkWorkspace(standalonePreview && bridgeKind === 'mock')
  const [forkPage, setForkPage] = React.useState<'live' | null>(null)
  const mainScrollRef = React.useRef<HTMLDivElement>(null)

  React.useEffect(() => {
    void motionStore.start()
  }, [])

  React.useEffect(() => {
    if (mainScrollRef.current) mainScrollRef.current.scrollTop = 0
  }, [page, forkPage])

  useMotionAppearanceEffect(appearance)
  useMotionTextInputBridge(textInputSupported)
  useMotionKeyboardShortcuts(ready && connected)

  return (
    <MotionErrorBoundary scope="app" onRestart={() => void motionStore.restart()}>
      {!ready || !connected ? (
        <BootScreen error={error ?? (ready ? '桥已断开' : null)} canReconnect={ready} />
      ) : (
        <TooltipProvider delayDuration={250}>
          <div className="relative flex h-full flex-col overflow-hidden">
            {bridgeKind === 'mock' && (
              <Alert variant="warning" className="rounded-none border-x-0 border-t-0 py-1.5" data-testid="mock-banner">
                <RiFlaskFill className="size-4" />
                <AlertDescription className="flex flex-wrap items-center gap-2 text-xs">
                  <span>
                    {standalonePreview
                      ? 'GakuMotion 独立前端预览模式（Mock Bridge）：数据为本地模拟，不影响游戏与生产构建。'
                      : '开发 Mock 模式：数据为本地模拟，不代表真实插件行为；未实现功能标记为“待迁移”。'}
                  </span>
                  {standalonePreview && (
                    <PreviewToolbar
                      appearance={appearance ?? { theme: 0, uiScale: 1 }}
                      injectionAllowed={permission.injectionAllowed}
                    />
                  )}
                </AlertDescription>
              </Alert>
            )}
            {permission.injectionAllowed ? (
              <>
                <div className="flex min-h-0 flex-1 overflow-hidden">
                  <MotionSidebar onNavigate={() => setForkPage(null)} />
                  <main className="flex min-w-0 flex-1 flex-col overflow-hidden">
                    <MotionStatusBar />
                    {fork.state && (fork.state.observed || fork.error) && (
                      <div className="flex flex-wrap items-center gap-2 border-b bg-card/60 px-5 py-2">
                        {forkPage && <Button size="sm" variant="ghost" onClick={() => setForkPage(null)}>返回{page === 'library' ? '曲库' : '当前页面'}</Button>}
                        {fork.state.observed && <Button size="sm" variant={forkPage === 'live' ? 'secondary' : 'outline'}
                          onClick={() => setForkPage('live')}>{fork.state.active ? 'Live 接管中' : 'Live 接管'}</Button>}
                        {fork.error && <span role="alert" className="text-xs text-destructive">{fork.error}</span>}
                      </div>
                    )}
                    <div ref={mainScrollRef} className="min-h-0 flex-1 overflow-y-auto px-5 py-4">
                      <MotionErrorBoundary
                        scope="page"
                        resetKey={page}
                        onResync={() => void motionStore.resync()}
                        onNavigateLibrary={() => motionStore.setPage('library')}
                      >
                        {forkPage === 'live' && fork.state?.observed ? <LiveControls state={fork.state} command={fork.command} />
                          : <ActivePage fork={fork.state} command={fork.command} />}
                      </MotionErrorBoundary>
                    </div>
                  </main>
                </div>
                <TransportBar />
              </>
            ) : (
              <div className="flex min-h-0 flex-1 flex-col">
                {fork.state?.observed && <div className="shrink-0 space-y-2 p-4">
                  <LiveControls state={fork.state} command={fork.command} />
                  {fork.error && <p role="alert" className="text-xs text-destructive">{fork.error}</p>}
                </div>}
                <PermissionGate />
              </div>
            )}
            <DropOverlay />
            <ImportProgressModal />
          </div>
        </TooltipProvider>
      )}
    </MotionErrorBoundary>
  )
}
