import { AlertTriangle, Info } from 'lucide-react'
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
import { PendingBadge } from '../components/Unsupported'

const SLOT_ORDER: ImportSlotKey[] = ['motion', 'facial', 'camera', 'audio']
const SLOT_LABEL: Record<ImportSlotKey, string> = {
  motion: '动作',
  facial: '表情',
  camera: '镜头',
  audio: '音频',
}

function CandidateBadge({ path }: { path: string }) {
  const probe = useMotionStore((state) => (isSlotPlaceholder(path) ? null : state.importForm.probes[path] ?? null))
  if (!probe) return null
  return <Badge variant="secondary">{vmdBadgeText(probe)}</Badge>
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
            每个槽位独立设置播放速度（0=停走）与初始偏移秒；速度与全局倍速相乘。
          </DialogDescription>
        </DialogHeader>

        {!importSupported && (
          <Alert variant="warning">
            <Info />
            <AlertDescription>当前原生桥未提供导入管线（library.import），无法真正导入。</AlertDescription>
          </Alert>
        )}
        {motionStore.isEditingActiveTrack() && (
          <Alert variant="warning">
            <AlertTriangle />
            <AlertDescription>
              正在编辑当前播放曲目；替换资源前需先停止播放。
            </AlertDescription>
          </Alert>
        )}

        <div className="space-y-4">
          <div className="space-y-1.5">
            <Label htmlFor="import-title">曲目标题</Label>
            <Input
              id="import-title"
              placeholder="自动根据文件名推荐"
              value={form.title}
              onChange={(event) => motionStore.setImportTitle(event.target.value)}
            />
          </div>

          <div className="space-y-2">
            <div className="flex items-center justify-between">
              <p className="text-sm font-medium">文件槽位</p>
              <div className="flex items-center gap-2">
                <Button
                  variant="outline"
                  size="sm"
                  disabled={busy || !dialogSupported}
                  onClick={() => void motionStore.browseFiles('append')}
                >
                  追加文件…
                </Button>
                {!dialogSupported && <PendingBadge />}
              </div>
            </div>
            <div className="rounded-md border">
              {SLOT_ORDER.map((slot, index) => {
                const slotState = form.slots[slot]
                const pick = picks[slot]
                const timing = form.timing[slot]
                return (
                  <div key={slot} className={`grid grid-cols-[72px_1fr_220px] items-center gap-2 p-2 ${index > 0 ? 'border-t' : ''}`}>
                    <div className="flex items-center gap-1.5">
                      <span className="text-sm">{SLOT_LABEL[slot]}</span>
                      <CandidateBadge path={pick} />
                    </div>
                    <div className="flex gap-2">
                      <Select
                        value={String(slotState.selection)}
                        onValueChange={(value) => motionStore.setImportSelection(slot, Number(value))}
                      >
                        <SelectTrigger className="h-8 flex-1" aria-label={`${SLOT_LABEL[slot]}槽位`}>
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
                        disabled={busy || !dialogSupported}
                        onClick={() => void motionStore.browseFiles(slot)}
                      >
                        浏览…
                      </Button>
                    </div>
                    <div className="flex items-center gap-2">
                      <Input
                        className="h-8"
                        type="number"
                        min={0}
                        step={0.01}
                        title="播放速度（0=停走）"
                        value={timing.speed}
                        onChange={(event) => motionStore.setImportTiming(slot, { speed: Number(event.target.value) })}
                      />
                      <Input
                        className="h-8"
                        type="number"
                        title="初始偏移秒"
                        value={timing.offsetSeconds}
                        onChange={(event) => motionStore.setImportTiming(slot, { offsetSeconds: Number(event.target.value) })}
                      />
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
