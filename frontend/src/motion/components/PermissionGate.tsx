import { RiShieldKeyholeFill, RiWrenchFill } from '@remixicon/react'
import { Alert, AlertDescription, AlertTitle } from '@/components/ui/alert'
import { Switch } from '@/components/ui/switch'
import { motionStore, useMotionStore } from '../store'
import { StageReplacementTab, StudioTab } from '../pages/DeveloperPage'
import { MotionStatusBar } from './MotionStatusBar'
import { CollapsiblePropertySection, PropertyRow, PropertySection } from './PropertyGroup'
import { UnsupportedPanel } from './Unsupported'
import type { ForkState } from '../protocol'

const DEFAULT_NOTICE =
  'GakuMotion 可在支持的 Live、拍摄现场和更衣室使用。'

// injectionAllowed=false replaces the whole shell (no sidebar/transport), but
// keeps the status bar and the developer options, exactly like GkGuiOverlay.
export function PermissionGate({ live }: { live?: ForkState | null }) {
  const liveReady = !!live?.viewing && live.available && !live.active
  const notice = useMotionStore((state) => state.permission.notice)
  const developerMode = useMotionStore((state) => state.developerMode)
  const stage = useMotionStore((state) => state.stage)
  const capabilities = useMotionStore((state) => state.capabilities)
  const replacementSupported = capabilities.includes('stage.setLoadReplacement')
  const studioSupported = capabilities.includes('studio.set')

  return (
    <div className="flex h-full flex-col overflow-hidden" data-testid="permission-gate">
      <MotionStatusBar />
      <div className="min-h-0 flex-1 overflow-y-auto px-5 py-5">
        <div className="mx-auto max-w-3xl space-y-4">
          <Alert variant={liveReady ? 'default' : 'destructive'}
            className={liveReady ? 'rounded-lg border-emerald-500/50 bg-emerald-500/10 p-3.5 text-emerald-400' : 'rounded-lg p-3.5'}>
            <RiShieldKeyholeFill className="size-4" />
            <AlertTitle className="text-sm font-semibold">{liveReady ? '已识别 Live' : '控制已禁用'}</AlertTitle>
            <AlertDescription className="mt-1 text-xs leading-relaxed">
              {liveReady ? '请启动 Live 接管后使用 GakuMotion。' : DEFAULT_NOTICE}
              {!liveReady && notice && !/仅支持已验证的偶像摄影场景与更衣室/.test(notice) && (
                <p className="mt-1">{notice}</p>
              )}
            </AlertDescription>
          </Alert>

          {stage.settings.loadReplacementKey !== '' && (
            <div className="rounded-lg border border-warning/40 bg-warning/5 px-3.5 py-2.5 text-xs text-muted-foreground">
              已保留实验舞台选择（<code className="font-mono text-foreground">{stage.settings.loadReplacementKey}</code>）；可在下方开发者选项中修改或恢复原生背景。
            </div>
          )}

          <CollapsiblePropertySection
            title="开发者选项"
            description="在控制权限受限的场景下仍可配置摄影舞台替换与摄影棚实验开关"
            summary={<RiWrenchFill className="size-3.5 text-muted-foreground" />}
            bodyClassName="space-y-3"
          >
            <PropertySection className="border-0">
              <PropertyRow
                label="开发者模式"
                description="开启后可配置尚未完成的摄影舞台替换与布光视频实验功能。"
                htmlFor="permission-developer-mode"
              >
                <Switch
                  id="permission-developer-mode"
                  checked={developerMode}
                  onCheckedChange={(checked) => motionStore.setDeveloperMode(checked)}
                />
              </PropertyRow>
            </PropertySection>

            {developerMode ? (
              <div className="space-y-3">
                {replacementSupported ? (
                  <PropertySection title="摄影舞台替换" bodyClassName="p-3.5">
                    <StageReplacementTab />
                  </PropertySection>
                ) : (
                  <UnsupportedPanel
                    title="摄影舞台替换"
                    description="下次摄影背景载入时替换；权限禁用时也可编辑。原生方法接入后可用。"
                    methods={['stage.setLoadReplacement']}
                  />
                )}
                {studioSupported ? (
                  <PropertySection title="布光与视频（实验）" bodyClassName="p-3.5">
                    <StudioTab />
                  </PropertySection>
                ) : (
                  <UnsupportedPanel
                    title="布光与视频（实验）"
                    description="自定义布光与本地视频幕布播放。原生方法接入后可用。"
                    methods={['studio.*']}
                  />
                )}
              </div>
            ) : (
              <p className="text-xs text-muted-foreground">开启开发者模式后可配置尚未完成的摄影舞台替换。</p>
            )}
          </CollapsiblePropertySection>
        </div>
      </div>
    </div>
  )
}
