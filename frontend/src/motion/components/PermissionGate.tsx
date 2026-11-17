import { ShieldAlert } from 'lucide-react'
import { Alert, AlertDescription, AlertTitle } from '@/components/ui/alert'
import { Collapsible, CollapsibleContent, CollapsibleTrigger } from '@/components/ui/collapsible'
import { Label } from '@/components/ui/label'
import { Switch } from '@/components/ui/switch'
import { motionStore, useMotionStore } from '../store'
import { StageReplacementTab, StudioTab } from '../pages/DeveloperPage'
import { MotionStatusBar } from './MotionStatusBar'
import { UnsupportedPanel } from './Unsupported'

const DEFAULT_NOTICE =
  'GakuMotion 当前已禁用。仅在偶像摄影或更衣室且截图保护就绪时可用；已保存的设置保持不变。'

// injectionAllowed=false replaces the whole shell (no sidebar/transport), but
// keeps the status bar and the developer options, exactly like GkGuiOverlay.
export function PermissionGate() {
  const permission = useMotionStore((state) => state.permission)
  const developerMode = useMotionStore((state) => state.developerMode)
  const stage = useMotionStore((state) => state.stage)
  const capabilities = useMotionStore((state) => state.capabilities)
  const replacementSupported = capabilities.includes('stage.setLoadReplacement')
  const studioSupported = capabilities.includes('studio.set')

  return (
    <div className="flex h-full flex-col" data-testid="permission-gate">
      <MotionStatusBar />
      <div className="min-h-0 flex-1 space-y-4 overflow-y-auto p-4">
        <Alert variant="destructive">
          <ShieldAlert />
          <AlertTitle>控制已禁用</AlertTitle>
          <AlertDescription>{permission.notice || DEFAULT_NOTICE}</AlertDescription>
        </Alert>
        {stage.settings.loadReplacementKey !== '' && (
          <p className="text-sm text-muted-foreground">
            已保留实验舞台选择；可在下方开发者选项中修改或恢复原生背景。
          </p>
        )}
        <Collapsible>
          <CollapsibleTrigger className="text-sm font-medium underline-offset-4 hover:underline">
            开发者选项
          </CollapsibleTrigger>
          <CollapsibleContent className="mt-3 space-y-4">
            <div className="flex items-center gap-2">
              <Switch
                id="permission-developer-mode"
                checked={developerMode}
                onCheckedChange={(checked) => motionStore.setDeveloperMode(checked)}
              />
              <Label htmlFor="permission-developer-mode">开发者模式</Label>
            </div>
            {developerMode ? (
              <>
                {replacementSupported ? (
                  <section className="space-y-2 rounded-md border p-4">
                    <h3 className="text-sm font-medium">摄影舞台替换</h3>
                    <StageReplacementTab />
                  </section>
                ) : (
                  <UnsupportedPanel
                    title="摄影舞台替换"
                    description="下次摄影背景载入时替换；权限禁用时也可编辑。原生方法接入后可用。"
                    methods={['stage.setLoadReplacement']}
                  />
                )}
                {studioSupported ? (
                  <section className="space-y-2 rounded-md border p-4">
                    <h3 className="text-sm font-medium">布光与视频（实验）</h3>
                    <StudioTab />
                  </section>
                ) : (
                  <UnsupportedPanel
                    title="布光与视频（实验）"
                    description="自定义布光与本地视频幕布播放。原生方法接入后可用。"
                    methods={['studio.*']}
                  />
                )}
              </>
            ) : (
              <p className="text-sm text-muted-foreground">开启开发者模式后可配置尚未完成的摄影舞台替换。</p>
            )}
          </CollapsibleContent>
        </Collapsible>
      </div>
    </div>
  )
}
