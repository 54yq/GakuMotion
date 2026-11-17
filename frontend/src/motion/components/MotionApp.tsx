import * as React from 'react'
import { FlaskConical, Loader2, PlugZap, RefreshCw } from 'lucide-react'
import { Alert, AlertDescription } from '@/components/ui/alert'
import { Button } from '@/components/ui/button'
import { TooltipProvider } from '@/components/ui/tooltip'
import { motionStore, useMotionStore } from '../store'
import { getMotionBridge, MotionMockBridgeClient, setMotionBridge } from '../bridge'
import { useMotionAppearanceEffect, useMotionTextInputBridge } from '../hooks'
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
    <div className="flex h-full flex-col items-center justify-center gap-3 text-muted-foreground">
      {error ? (
        <>
          <PlugZap className="size-8 text-destructive" />
          <p className="max-w-md text-center text-sm">GakuMotion 桥不可用：{error}</p>
          {canReconnect && (
            <Button type="button" variant="outline" size="sm" onClick={() => void motionStore.restart()}>
              <RefreshCw className="size-3.5" />
              重新连接
            </Button>
          )}
        </>
      ) : (
        <>
          <Loader2 className="size-7 animate-spin text-primary" />
          <p className="text-sm">正在连接 GakuMotion…</p>
        </>
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
        <RefreshCw className="size-3" />
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

  return (
    <MotionErrorBoundary scope="app" onRestart={() => void motionStore.restart()}>
      {!ready || !connected ? (
        <BootScreen error={error ?? (ready ? '桥已断开' : null)} canReconnect={ready} />
      ) : (
        <TooltipProvider delayDuration={250}>
          <div className="relative flex h-full flex-col">
            {bridgeKind === 'mock' && (
              <Alert variant="warning" className="rounded-none border-x-0 border-t-0 py-1.5" data-testid="mock-banner">
                <FlaskConical />
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
                <div className="flex min-h-0 flex-1">
                  <MotionSidebar />
                  <main className="flex min-w-0 flex-1 flex-col">
                    <MotionStatusBar />
                    <div ref={mainScrollRef} className="min-h-0 flex-1 overflow-y-auto p-4">
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
