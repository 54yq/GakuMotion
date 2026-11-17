import * as React from 'react'
import { RiFlaskFill, RiLoader4Fill, RiPlugFill, RiRefreshFill, RiUploadCloud2Fill } from '@remixicon/react'
import { Alert, AlertDescription } from '@/components/ui/alert'
import { Button } from '@/components/ui/button'
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

function ActivePage() {
  const page = useMotionStore((state) => state.page)
  switch (page) {
    case 'library':
      return <LibraryPage />
    case 'motion':
      return <MotionPage />
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
  if (!mock) return null
  return (
    <div className="ml-auto flex flex-wrap items-center gap-2" data-testid="preview-toolbar">
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
  const mainScrollRef = React.useRef<HTMLDivElement>(null)

  React.useEffect(() => {
    void motionStore.start()
  }, [])

  React.useEffect(() => {
    if (mainScrollRef.current) mainScrollRef.current.scrollTop = 0
  }, [page])

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
                  <MotionSidebar />
                  <main className="flex min-w-0 flex-1 flex-col overflow-hidden">
                    <MotionStatusBar />
                    <div ref={mainScrollRef} className="min-h-0 flex-1 overflow-y-auto px-5 py-4">
                      <MotionErrorBoundary
                        scope="page"
                        resetKey={page}
                        onResync={() => void motionStore.resync()}
                        onNavigateLibrary={() => motionStore.setPage('library')}
                      >
                        <ActivePage />
                      </MotionErrorBoundary>
                    </div>
                  </main>
                </div>
                <TransportBar />
              </>
            ) : (
              <PermissionGate />
            )}
            <DropOverlay />
            <ImportProgressModal />
          </div>
        </TooltipProvider>
      )}
    </MotionErrorBoundary>
  )
}
