import {
  RiAlertFill,
  RiCameraFill,
  RiEmotionHappyFill,
  RiFolderAddFill,
  RiInformationFill,
  RiMusic2Fill,
  RiSparkling2Fill,
  type RemixiconComponentType,
} from '@remixicon/react'
import { Alert, AlertDescription } from '@/components/ui/alert'
import { Badge } from '@/components/ui/badge'
import { Button } from '@/components/ui/button'
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from '@/components/ui/dialog'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { IMPORT_SLOT, isSlotPlaceholder, vmdBadgeText } from '../protocol'
import { motionStore, useMotionStore, type ImportSlotKey } from '../store'
import { DragFloat } from '../components/DragFloat'
import { PendingBadge } from '../components/Unsupported'

const SLOT_ORDER: ImportSlotKey[] = ['motion', 'facial', 'camera', 'audio']
const SLOT_META: Record<ImportSlotKey, { label: string; hint: string; icon: RemixiconComponentType }> = {
  motion: { label: '动作', hint: '身体骨骼动作 (.vmd)', icon: RiSparkling2Fill },
  facial: { label: '表情', hint: '口型与眼眉表情 (.vmd)', icon: RiEmotionHappyFill },
  camera: { label: '镜头', hint: 'VMD 镜头或烘焙 .gkcamera', icon: RiCameraFill },
  audio: { label: '音频', hint: '演出配乐 (.wav 无损)', icon: RiMusic2Fill },
}

function CandidateBadge({ path }: { path: string }) {
  const probe = useMotionStore((state) => (isSlotPlaceholder(path) ? null : state.importForm.probes[path] ?? null))
  if (!probe) return null
  return (
    <Badge variant="outline" className="rounded-[4px] border-border bg-muted/60 px-1.5 py-0 text-[10px] font-medium text-muted-foreground">
      {vmdBadgeText(probe)}
    </Badge>
  )
}

export function ImportDialog() {
  const open = useMotionStore((state) => state.importForm.open)
  const form = useMotionStore((state) => state.importForm)
  const busy = useMotionStore((state) => state.dialogBusy)
  const unsupportedAudio = useMotionStore((state) => state.importForm.unsupportedAudio)
  const capabilities = useMotionStore((state) => state.capabilities)
  const supports = (method: string) => capabilities.includes(method)
  const importSupported = supports('library.import')
  const dialogSupported = supports('dialog.pickFile')
  const picks = motionStore.importFormPicks()
  const formError = motionStore.importFormError()
  const editing = form.mode === 'edit'

  return (
    <Dialog open={open} onOpenChange={(next) => { if (!next) motionStore.closeImportForm() }}>
      <DialogContent className="max-w-3xl" data-testid="import-dialog">
        <DialogHeader>
          <DialogTitle>{editing ? '管理选中曲目' : '添加到曲库'}</DialogTitle>
          <DialogDescription>
            为每个轨道分配源文件并设置独立播放速率（0 = 停走）与初始偏移秒；轨道速率会与全局倍速相乘。
          </DialogDescription>
        </DialogHeader>

        {!importSupported && (
          <Alert variant="warning">
            <RiInformationFill className="size-4" />
            <AlertDescription>当前原生桥未提供导入管线（library.import），无法真正导入。</AlertDescription>
          </Alert>
        )}
        {motionStore.isEditingActiveTrack() && (
          <Alert variant="warning">
            <RiAlertFill className="size-4" />
            <AlertDescription>
              正在编辑当前播放曲目；替换资源前需先停止播放。
            </AlertDescription>
          </Alert>
        )}

        <div className="space-y-4">
          <div className="grid gap-3 sm:grid-cols-2">
            <div className="space-y-1.5">
              <Label htmlFor="import-title">曲目标题</Label>
              <Input
                id="import-title"
                placeholder="自动根据文件名推荐"
                value={form.title}
                onChange={(event) => motionStore.setImportTitle(event.target.value)}
              />
            </div>
            <div className="space-y-1.5">
              <Label htmlFor="import-subtitle">副标题 / 作者</Label>
              <Input
                id="import-subtitle"
                placeholder="可选，多个作者用半角逗号 , 分隔"
                value={form.subtitle}
                onChange={(event) => motionStore.setImportSubtitle(event.target.value)}
              />
            </div>
          </div>

          <div className="space-y-2.5">
            <div className="flex items-center justify-between">
              <p className="text-sm font-medium">演出轨道与同步参数</p>
              <div className="flex items-center gap-2">
                <Button
                  variant="outline"
                  size="sm"
                  disabled={busy || !dialogSupported}
                  onClick={() => void motionStore.browseFiles('append')}
                >
                  <RiFolderAddFill className="size-3.5" />
                  追加文件…
                </Button>
                {!dialogSupported && <PendingBadge />}
              </div>
            </div>

            <div className="divide-y divide-border/60 rounded-lg border bg-card">
              {SLOT_ORDER.map((slot) => {
                const slotState = form.slots[slot]
                const pick = picks[slot]
                const timing = form.timing[slot]
                const meta = SLOT_META[slot]
                const SlotIcon = meta.icon
                return (
                  <div
                    key={slot}
                    className="grid gap-2.5 p-3 sm:grid-cols-[150px_1fr_196px] sm:items-center"
                  >
                    <div className="space-y-0.5">
                      <div className="flex items-center gap-1.5">
                        <SlotIcon className="size-3.5 text-primary" />
                        <span className="text-sm font-medium">{meta.label}</span>
                        <CandidateBadge path={pick} />
                      </div>
                      <p className="text-[11px] text-muted-foreground">{meta.hint}</p>
                    </div>

                    <div className="flex min-w-0 gap-1.5">
                      <Select
                        value={String(slotState.selection)}
                        onValueChange={(value) => motionStore.setImportSelection(slot, Number(value))}
                      >
                        <SelectTrigger className="h-8 min-w-0 flex-1 text-xs" aria-label={`${meta.label}槽位`}>
                          <SelectValue placeholder={IMPORT_SLOT.empty} />
                        </SelectTrigger>
                        <SelectContent>
                          {slotState.candidates.map((candidate, candidateIndex) => (
                            <SelectItem key={candidate} value={String(candidateIndex)}>
                              {candidate}
                            </SelectItem>
                          ))}
                        </SelectContent>
                      </Select>
                      <Button
                        variant="outline"
                        size="sm"
                        className="h-8 shrink-0 px-2.5 text-xs"
                        disabled={busy || !dialogSupported}
                        onClick={() => void motionStore.browseFiles(slot)}
                      >
                        浏览…
                      </Button>
                    </div>

                    <div className="grid grid-cols-2 gap-1.5">
                      <div className="space-y-0.5 text-[11px] text-muted-foreground">
                        <span>速率 (倍)</span>
                        <DragFloat
                          ariaLabel={`${meta.label}速率`}
                          inputClassName="h-7 w-full px-2 text-xs"
                          value={timing.speed}
                          min={0}
                          max={10}
                          step={0.01}
                          decimals={2}
                          onChange={(next) => motionStore.setImportTiming(slot, { speed: next })}
                          onCommit={(next) => motionStore.setImportTiming(slot, { speed: next })}
                        />
                      </div>
                      <div className="space-y-0.5 text-[11px] text-muted-foreground">
                        <span>偏移 (秒)</span>
                        <DragFloat
                          ariaLabel={`${meta.label}偏移`}
                          inputClassName="h-7 w-full px-2 text-xs"
                          value={timing.offsetSeconds}
                          min={-3600}
                          max={3600}
                          step={0.01}
                          decimals={2}
                          onChange={(next) => motionStore.setImportTiming(slot, { offsetSeconds: next })}
                          onCommit={(next) => motionStore.setImportTiming(slot, { offsetSeconds: next })}
                        />
                      </div>
                    </div>
                  </div>
                )
              })}
            </div>

            {unsupportedAudio > 0 && (
              <p className="text-xs text-muted-foreground">
                {unsupportedAudio} 个非 WAV 音频未自动填入，可用“追加文件”后手动选择。
              </p>
            )}
          </div>

          {form.activeTrackWarning && <p className="text-sm text-destructive">{form.activeTrackWarning}</p>}
          {formError && importSupported && (
            <p className="text-xs text-muted-foreground">提示：{formError}</p>
          )}
        </div>

        <DialogFooter>
          <Button variant="outline" onClick={() => motionStore.closeImportForm()}>
            取消
          </Button>
          {motionStore.isEditingActiveTrack() && (
            <Button variant="outline" disabled={form.pendingStopAndSave} onClick={() => void motionStore.stopPlaybackAndSave()}>
              停止并保存
            </Button>
          )}
          <Button
            disabled={!importSupported || !!formError || busy}
            title={importSupported ? undefined : '导入管线待迁移'}
            onClick={() => void motionStore.submitImport()}
          >
            {editing ? '保存修改' : '导入'}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  )
}
