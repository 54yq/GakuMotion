import * as React from 'react'
import { ChevronDown } from 'lucide-react'
import { Button } from '@/components/ui/button'
import { Badge } from '@/components/ui/badge'
import { Collapsible, CollapsibleContent, CollapsibleTrigger } from '@/components/ui/collapsible'
import { Checkbox } from '@/components/ui/checkbox'
import { Label } from '@/components/ui/label'
import { Separator } from '@/components/ui/separator'
import { motionStore, useMotionStore } from '../store'
import { PendingBadge } from '../components/Unsupported'

const COLOR_PRESETS: Array<{ label: string; rgb: [number, number, number] }> = [
  { label: '纯白', rgb: [1, 1, 1] },
  { label: '中灰', rgb: [0.5, 0.5, 0.5] },
  { label: '纯黑', rgb: [0, 0, 0] },
  { label: '绿幕', rgb: [0, 1, 0] },
  { label: '蓝幕', rgb: [0, 0.35, 0.85] },
]

function rgbToHex(rgb: [number, number, number]): string {
  return `#${rgb.map((channel) => Math.round(Math.min(Math.max(channel, 0), 1) * 255).toString(16).padStart(2, '0')).join('')}`
}

function hexToRgb(hex: string): [number, number, number] {
  const value = hex.replace('#', '')
  return [0, 2, 4].map((offset) => parseInt(value.slice(offset, offset + 2), 16) / 255) as [number, number, number]
}

export function StagePage() {
  const stage = useMotionStore((state) => state.stage)
  const rawNodes = useMotionStore((state) => state.stageNodes)
  const safeNodes = Array.isArray(rawNodes) ? rawNodes : []
  const supported = useMotionStore((state) => state.capabilities.includes('stage.set'))
  const scanSupported = useMotionStore((state) => state.capabilities.includes('stage.scan'))
  const [scanning, setScanning] = React.useState(false)
  const settings = stage.settings
  const sceneBgEnabled = !settings.hideBackground
  const customNodeActive = settings.customNodeControl
  const disabled = !supported

  const triggerScan = React.useCallback(async () => {
    if (!scanSupported || scanning) return
    setScanning(true)
    try {
      await motionStore.scanStageNodes()
    } finally {
      setScanning(false)
    }
  }, [scanSupported, scanning])

  return (
    <div className="space-y-5" data-testid="stage-page">
      {stage.statusNotice && (
        <p className={`text-sm ${/失败|错误/.test(stage.statusNotice) ? 'text-destructive' : 'text-muted-foreground'}`}>
          舞台状态: {stage.statusNotice}
        </p>
      )}
      {settings.loadReplacementKey !== '' && (
        <p className="text-sm text-muted-foreground">
          已保留实验舞台选择；请在 [开发] 页的“摄影舞台替换”中修改或恢复原生背景。
        </p>
      )}

      <section className="space-y-3">
        <div className="flex items-center gap-2">
          <h3 className="text-sm font-medium">舞台视觉控制</h3>
          {!supported && <PendingBadge />}
        </div>
        {customNodeActive && (
          <p className="text-sm text-warning">高级自定义节点已启用，以下自动管理选项暂时锁定。</p>
        )}
        <div className="flex items-center gap-2">
          <Checkbox
            id="stage-lighting"
            checked={settings.lightingEffectsEnabled}
            disabled={disabled || customNodeActive}
            onCheckedChange={(checked) => motionStore.patchStage({ lightingEffectsEnabled: checked === true })}
          />
          <Label htmlFor="stage-lighting" title="控制场景光影节点与角色阴影渲染；关闭场景背景时不会关闭此项">
            光影效果
          </Label>
        </div>
        <div className="flex items-center gap-2">
          <Checkbox
            id="stage-background"
            checked={sceneBgEnabled}
            disabled={disabled || customNodeActive}
            onCheckedChange={(checked) => {
              const hide = checked !== true
              motionStore.patchStage({
                hideBackground: hide,
                greenScreenActive: hide,
                overrideCameraClear: hide,
              })
            }}
          />
          <Label htmlFor="stage-background" title="关闭时自动隐藏场景网格并应用纯色背景">
            场景背景
          </Label>
        </div>
        <div className="flex flex-wrap items-center gap-3">
          <input
            type="color"
            aria-label="背景颜色"
            className="h-8 w-12 cursor-pointer rounded border border-input bg-card disabled:cursor-not-allowed disabled:opacity-50"
            value={rgbToHex([settings.greenScreenColor[0], settings.greenScreenColor[1], settings.greenScreenColor[2]])}
            disabled={disabled || customNodeActive || sceneBgEnabled}
            onChange={(event) => {
              const rgb = hexToRgb(event.target.value)
              motionStore.patchStage({ greenScreenColor: [rgb[0], rgb[1], rgb[2], 1] })
            }}
          />
          <span className="text-sm text-muted-foreground">背景颜色</span>
          <span className="text-xs text-muted-foreground">预置:</span>
          {COLOR_PRESETS.map((preset) => (
            <button
              key={preset.label}
              type="button"
              title={`${preset.label} (${preset.rgb.join(', ')})`}
              aria-label={`背景预置 ${preset.label}`}
              className="size-6 rounded border border-input disabled:cursor-not-allowed disabled:opacity-50"
              style={{ backgroundColor: rgbToHex(preset.rgb) }}
              disabled={disabled || customNodeActive || sceneBgEnabled}
              onClick={() => motionStore.patchStage({ greenScreenColor: [preset.rgb[0], preset.rgb[1], preset.rgb[2], 1] })}
            />
          ))}
        </div>
      </section>

      <Separator />

      <Collapsible>
        <CollapsibleTrigger className="flex items-center gap-1.5 text-sm font-medium">
          <ChevronDown className="size-4" />
          高级场景编辑
          {!supported && <PendingBadge />}
        </CollapsibleTrigger>
        <CollapsibleContent className="mt-3 space-y-3">
          <div className="flex items-center gap-2">
            <Checkbox
              id="stage-custom-nodes"
              checked={settings.customNodeControl}
              disabled={disabled}
              onCheckedChange={(checked) => {
                const enabled = checked === true
                motionStore.patchStage({ customNodeControl: enabled })
                if (enabled && safeNodes.length === 0 && scanSupported) void triggerScan()
              }}
            />
            <Label htmlFor="stage-custom-nodes">自定义场景节点 [开关]</Label>
          </div>
          {settings.customNodeControl ? (
            <>
              <p className="text-sm text-warning">
                启用后将停止 GakuMotion 对场景节点的自动管理。仅建议在需要手工定制场景时使用。
              </p>
              <div className="flex items-center gap-2">
                <Button
                  variant="outline"
                  size="sm"
                  disabled={!scanSupported || scanning}
                  onClick={() => void triggerScan()}
                >
                  {scanning ? '正在扫描…' : '扫描场景节点'}
                </Button>
                {!scanSupported && <PendingBadge />}
              </div>
              <div className="max-h-72 space-y-1 overflow-y-auto rounded-md border p-2" data-testid="stage-node-tree">
                {safeNodes.length === 0 ? (
                  <p className="text-sm text-muted-foreground">尚未扫描或没有候选节点。</p>
                ) : (
                  safeNodes.map((node) => {
                    const parentInactive = safeNodes.some(
                      (candidate) => candidate.index === node.parentIndex && !candidate.currentlyActive,
                    )
                    return (
                      <div
                        key={node.index}
                        className="flex items-center gap-2 py-0.5"
                        style={{ paddingLeft: `${node.depth * 14}px` }}
                      >
                        <Checkbox
                          checked={node.currentlyActive}
                          disabled={node.isColliderLocked || parentInactive || !scanSupported}
                          onCheckedChange={(checked) => void motionStore.toggleStageNode(node.index, checked === true)}
                        />
                        <span className="text-sm">{node.displayName}</span>
                        {node.isLightingRoot && <Badge variant="secondary">光影</Badge>}
                        {node.isIdentifiedBackground && <Badge variant="secondary">背景</Badge>}
                        {node.isColliderLocked && <Badge variant="outline">碰撞锁定</Badge>}
                      </div>
                    )
                  })
                )}
              </div>
            </>
          ) : (
            <p className="text-sm text-muted-foreground">
              开启自定义场景节点后，可手工逐项勾选/隐藏场景里的特定 Mesh 或物件。
            </p>
          )}
        </CollapsibleContent>
      </Collapsible>
    </div>
  )
}
