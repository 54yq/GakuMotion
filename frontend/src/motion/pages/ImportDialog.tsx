import * as React from 'react'
import {
  RiAlertFill,
  RiCameraFill,
  RiDeleteBinLine,
  RiEmotionHappyFill,
  RiFolderAddFill,
  RiImageEditFill,
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
import { cn } from '@/lib/utils'
import { IMPORT_SLOT, isSlotPlaceholder, vmdBadgeText } from '../protocol'
import { motionStore, useMotionStore, type ImportSlotKey } from '../store'
import { isSupportedImageExtension } from '../validation'
import { AlbumCover } from '../components/AlbumCover'
import { CoverEditorModal } from '../components/CoverEditorModal'
import { DragFloat } from '../components/DragFloat'
import { PendingBadge } from '../components/Unsupported'

const SLOT_ORDER: ImportSlotKey[] = ['motion', 'facial', 'camera', 'audio']
const SLOT_META: Record<ImportSlotKey, { label: string; hint: string; icon: RemixiconComponentType }> = {
  motion: { label: '动作', hint: '身体骨骼动作 (.vmd)', icon: RiSparkling2Fill },
  facial: { label: '表情', hint: '口型与眼眉表情 (.vmd)', icon: RiEmotionHappyFill },
  camera: { label: '镜头', hint: 'VMD 镜头或烘焙 .gkcamera', icon: RiCameraFill },
  audio: { label: '音频', hint: '本地音频 (.mp3 / .flac / .wav / .ogg / .m4a / .aac / .opus)', icon: RiMusic2Fill },
}

function CandidateBadge({ path }: { path: string }) {
  const probe = useMotionStore((state) => (isSlotPlaceholder(path) ? null : state.importForm.probes[path] ?? null))
  if (!probe) return null
  return (
    <Badge variant="outline" className="rounded-[4px] border-border bg-muted/60 px-1.5 py-0 text-micro font-medium text-muted-foreground">
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
  const [coverDragOver, setCoverDragOver] = React.useState(false)

  const resolvedCoverPreview =
    form.coverMode === 'clear'
      ? ''
      : form.coverMode === 'custom'
        ? form.coverDataUri
        : form.audioProbe?.hasCover && form.audioProbe.coverDataUri
          ? form.audioProbe.coverDataUri
          : form.coverDataUri

  const handleCoverDrop = (event: React.DragEvent<HTMLDivElement>) => {
    event.preventDefault()
    setCoverDragOver(false)
    const file = event.dataTransfer.files?.[0]
    if (!file) return
    if (!file.type.startsWith('image/') && !isSupportedImageExtension(file.name)) return
    const reader = new FileReader()
    reader.onload = () => {
      if (typeof reader.result === 'string' && reader.result.startsWith('data:image/')) {
        motionStore.openCoverEditorForImport()
        motionStore.setCoverEditorImageUri(reader.result)
      }
    }
    reader.readAsDataURL(file)
  }

  return (
    <>
      <Dialog open={open} onOpenChange={(next) => { if (!next) motionStore.closeImportForm() }}>
        <DialogContent
          className="flex max-h-[85vh] w-[calc(100vw-2rem)] max-w-3xl flex-col gap-0 overflow-hidden p-0"
          data-testid="import-dialog"
        >
          <DialogHeader className="shrink-0 border-b border-border/60 px-5 pt-5 pb-3 pr-10">
            <DialogTitle>{editing ? '管理选中曲目' : '添加到曲库'}</DialogTitle>
            <DialogDescription>
              为每个轨道分配源文件并设置独立播放速率（0 = 停走）与初始偏移秒；轨道速率会与全局倍速相乘。
            </DialogDescription>
          </DialogHeader>

          <div className="min-h-0 flex-1 space-y-4 overflow-y-auto px-5 py-4">
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

            <div className="grid gap-3 sm:grid-cols-2">
              <div className="min-w-0 space-y-1.5">
                <Label htmlFor="import-title">曲目标题</Label>
                <Input
                  id="import-title"
                  placeholder="自动根据文件名推荐"
                  value={form.title}
                  onChange={(event) => motionStore.setImportTitle(event.target.value)}
                />
              </div>
              <div className="min-w-0 space-y-1.5">
                <Label htmlFor="import-subtitle">副标题 / 作者</Label>
                <Input
                  id="import-subtitle"
                  placeholder="可选，多个作者用半角逗号 , 分隔"
                  value={form.subtitle}
                  onChange={(event) => motionStore.setImportSubtitle(event.target.value)}
                />
              </div>
            </div>

            {/* Cover Image Slot */}
            <div
              onDragOver={(event) => {
                event.preventDefault()
                setCoverDragOver(true)
              }}
              onDragLeave={() => setCoverDragOver(false)}
              onDrop={handleCoverDrop}
              data-testid="import-cover-slot"
              className={cn(
                'flex flex-wrap items-center gap-3 rounded-lg border bg-card p-3 transition-colors',
                coverDragOver ? 'border-primary bg-primary/5' : 'border-border',
              )}
            >
              <button
                type="button"
                onClick={() => motionStore.openCoverEditorForImport()}
                className="relative size-14 shrink-0 overflow-hidden rounded-md border border-border/70 bg-muted/40 transition-opacity hover:opacity-90"
                title="点击裁剪或更换 1:1 封面"
              >
                <AlbumCover
                  src={resolvedCoverPreview}
                  imgTestId="import-slot-cover-img"
                  defaultTestId="import-slot-default-cover"
                />
              </button>
              <div className="min-w-0 flex-1 space-y-0.5">
                <div className="text-base font-medium text-foreground">曲目封面 (1:1)</div>
                <p className="text-xs text-muted-foreground">
                  {form.coverMode === 'custom'
                    ? '已配置自定义裁切封面；导入时将写入 cover.png。'
                    : form.coverMode === 'clear'
                      ? '已选择清除封面（将使用默认封面）。'
                      : resolvedCoverPreview
                        ? '已自动读取曲目/音频内嵌封面，可点击右侧按钮进入 1:1 裁切或重拍。'
                        : '可选：拖入图片（.png/.jpg/.webp/.bmp）、选择文件，或从 ScreenshotManager 拍照。'}
                </p>
              </div>
              <div className="flex shrink-0 items-center gap-1.5">
                <Button
                  type="button"
                  variant="outline"
                  size="sm"
                  className="h-8 text-xs"
                  onClick={() => motionStore.openCoverEditorForImport()}
                  data-testid="import-edit-cover-btn"
                >
                  <RiImageEditFill className="size-3.5" />
                  编辑封面 / 拍照…
                </Button>
                {(resolvedCoverPreview || form.coverMode === 'custom') && (
                  <Button
                    type="button"
                    variant="ghost"
                    size="sm"
                    className="h-8 px-2 text-xs text-muted-foreground hover:text-destructive"
                    onClick={() => motionStore.setImportCover('clear', '')}
                    title="移除封面"
                  >
                    <RiDeleteBinLine className="size-3.5" />
                    清除
                  </Button>
                )}
              </div>
            </div>

            <div className="space-y-2.5">
              <div className="flex items-center justify-between gap-2">
                <p className="text-base font-medium">演出轨道与同步参数</p>
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
                  const hasDiskPath = !isSlotPlaceholder(pick)
                  return (
                    <div
                      key={slot}
                      className="grid gap-2.5 p-3 sm:grid-cols-[150px_minmax(0,1fr)_196px] sm:items-center"
                    >
                      <div className="min-w-0 space-y-0.5">
                        <div className="flex flex-wrap items-center gap-1.5">
                          <SlotIcon className="size-3.5 shrink-0 text-primary" />
                          <span className="text-base font-medium">{meta.label}</span>
                          <CandidateBadge path={pick} />
                        </div>
                        <p className="text-xs text-muted-foreground">{meta.hint}</p>
                      </div>

                      <div className="min-w-0 space-y-1">
                        <div className="flex min-w-0 gap-1.5">
                          <Select
                            value={String(slotState.selection)}
                            onValueChange={(value) => motionStore.setImportSelection(slot, Number(value))}
                          >
                            <SelectTrigger
                              className="h-8 min-w-0 flex-1 overflow-hidden text-xs [&>span]:min-w-0 [&>span]:truncate"
                              aria-label={`${meta.label}槽位`}
                            >
                              <SelectValue placeholder={IMPORT_SLOT.empty} />
                            </SelectTrigger>
                            <SelectContent className="max-w-[min(36rem,calc(100vw-2rem))]">
                              {slotState.candidates.map((candidate, candidateIndex) => (
                                <SelectItem
                                  key={candidate}
                                  value={String(candidateIndex)}
                                  className="break-all [overflow-wrap:anywhere]"
                                >
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
                        {hasDiskPath && (
                          <p
                            className="min-w-0 break-all [overflow-wrap:anywhere] text-xs leading-snug text-muted-foreground"
                            title={pick}
                          >
                            {pick}
                          </p>
                        )}
                      </div>

                      <div className="grid grid-cols-2 gap-1.5">
                        <div className="space-y-0.5 text-xs text-muted-foreground">
                          <span>速率 (倍)</span>
                          <DragFloat
                            ariaLabel={`${meta.label}速率`}
                            inputClassName="h-8 w-full px-2 text-xs"
                            value={timing.speed}
                            min={0}
                            max={10}
                            step={0.01}
                            decimals={2}
                            onChange={(next) => motionStore.setImportTiming(slot, { speed: next })}
                            onCommit={(next) => motionStore.setImportTiming(slot, { speed: next })}
                          />
                        </div>
                        <div className="space-y-0.5 text-xs text-muted-foreground">
                          <span>偏移 (秒)</span>
                          <DragFloat
                            ariaLabel={`${meta.label}偏移`}
                            inputClassName="h-8 w-full px-2 text-xs"
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

              {form.audioProbe?.valid && (
                <div
                  data-testid="import-audio-preview"
                  className="flex min-w-0 items-center gap-3 rounded-lg border border-border/70 bg-muted/30 p-2.5"
                >
                  {form.audioProbe.hasCover && form.audioProbe.coverDataUri && (
                    <img
                      src={form.audioProbe.coverDataUri}
                      alt=""
                      data-testid="import-cover-preview"
                      className="size-12 shrink-0 rounded-md border border-border/60 object-cover"
                    />
                  )}
                  <div className="min-w-0 flex-1 space-y-0.5 text-sm">
                    <div className="truncate font-medium text-foreground">
                      {form.audioProbe.title || '音频元数据'}
                      {form.audioProbe.artist && (
                        <span className="ml-2 text-muted-foreground">· {form.audioProbe.artist}</span>
                      )}
                    </div>
                    <div className="text-xs text-muted-foreground tabular-nums">
                      {form.audioProbe.codec.toUpperCase()} · {Math.round(form.audioProbe.sampleRate / 100) / 10} kHz ·{' '}
                      {form.audioProbe.durationSeconds.toFixed(1)}s
                    </div>
                  </div>
                </div>
              )}

              {unsupportedAudio > 0 && (
                <p className="text-xs text-muted-foreground">
                  {unsupportedAudio} 个不支持的音频文件未自动填入，可用“追加文件”后手动选择。
                </p>
              )}

              <div className="grid gap-3 rounded-lg border bg-card/60 p-3 sm:grid-cols-2">
                <div className="space-y-1.5">
                  <Label htmlFor="import-source-fps" className="text-xs">源动作帧率</Label>
                  <Select
                    value={String(form.sourceFps)}
                    onValueChange={(value) => motionStore.setImportSourceFps(Number(value))}
                  >
                    <SelectTrigger id="import-source-fps" className="h-8 text-xs" aria-label="源动作帧率">
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="0">自动检测</SelectItem>
                      <SelectItem value="25">25 fps</SelectItem>
                      <SelectItem value="30">30 fps</SelectItem>
                      <SelectItem value="60">60 fps</SelectItem>
                    </SelectContent>
                  </Select>
                </div>
                <div className="space-y-1.5">
                  <Label htmlFor="import-output-fps" className="text-xs">输出采样帧率</Label>
                  <Select
                    value={String(form.fps)}
                    onValueChange={(value) => motionStore.setImportOutputFps(Number(value))}
                  >
                    <SelectTrigger id="import-output-fps" className="h-8 text-xs" aria-label="输出采样帧率">
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="0">自动（保持原有）</SelectItem>
                      <SelectItem value="30">30 fps</SelectItem>
                      <SelectItem value="60">60 fps</SelectItem>
                    </SelectContent>
                  </Select>
                </div>
                <p className="sm:col-span-2 text-xs text-muted-foreground">
                  自动检测依据身体骨骼与音频；修改已有曲目的帧率会重新烘焙动作。
                </p>
              </div>
            </div>

            {form.activeTrackWarning && (
              <p className="break-all [overflow-wrap:anywhere] text-sm text-destructive">{form.activeTrackWarning}</p>
            )}
            {formError && importSupported && (
              <p className="break-all [overflow-wrap:anywhere] text-xs text-muted-foreground">提示：{formError}</p>
            )}
          </div>

          <DialogFooter className="shrink-0 border-t border-border/60 bg-card px-5 py-3.5">
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

      <CoverEditorModal owner="import" />
    </>
  )
}
