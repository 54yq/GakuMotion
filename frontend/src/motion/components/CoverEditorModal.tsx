import * as React from 'react'
import {
  RiCameraFill,
  RiDeleteBinLine,
  RiImageAddFill,
  RiLoader4Line,
  RiRefreshLine,
  RiZoomInLine,
} from '@remixicon/react'
import { Alert, AlertDescription } from '@/components/ui/alert'
import { Button } from '@/components/ui/button'
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from '@/components/ui/dialog'
import { Slider } from '@/components/ui/slider'
import { cn } from '@/lib/utils'
import { motionStore, useMotionStore } from '../store'
import { isSupportedImageExtension } from '../validation'
import { DefaultAlbumCover } from './AlbumCover'

const CROP_VIEW_SIZE = 272
const OUTPUT_SIZE = 512

interface NaturalSize {
  width: number
  height: number
}

function clampPan(
  panX: number,
  panY: number,
  natural: NaturalSize,
  zoom: number,
): { x: number; y: number } {
  if (natural.width <= 0 || natural.height <= 0) return { x: 0, y: 0 }
  const baseScale = Math.max(CROP_VIEW_SIZE / natural.width, CROP_VIEW_SIZE / natural.height)
  const scaledW = natural.width * baseScale * zoom
  const scaledH = natural.height * baseScale * zoom
  const maxX = Math.max(0, (scaledW - CROP_VIEW_SIZE) / 2)
  const maxY = Math.max(0, (scaledH - CROP_VIEW_SIZE) / 2)
  return {
    x: Math.min(Math.max(panX, -maxX), maxX),
    y: Math.min(Math.max(panY, -maxY), maxY),
  }
}

export function CoverEditorModal({ owner }: { owner: 'library' | 'transport' | 'import' }) {
  const editor = useMotionStore((state) => state.coverEditor)
  const capabilities = useMotionStore((state) => state.capabilities)
  const dialogBusy = useMotionStore((state) => state.dialogBusy)
  const dropVisual = useMotionStore((state) => state.drop.visual)
  const isOwnerOpen = editor.open && editor.owner === owner

  const supports = (method: string) => capabilities.includes(method)
  const canUseHostFileDialog = supports('dialog.pickFile') && supports('library.readImagePreview')
  const screenshotSupported = supports('screenshot.capture')
  const caps = editor.screenshotCaps
  const canCaptureScreenshot =
    screenshotSupported &&
    !editor.capturing &&
    !editor.saving &&
    (caps === null ? false : caps.available && caps.runtimeReady && caps.cameraAvailable)

  const screenshotTooltip = !screenshotSupported
    ? '当前原生桥接不支持 ScreenshotManager 拍照联动'
    : caps === null
      ? '正在检测 ScreenshotManager 服务状态...'
      : !caps.available
        ? caps.notice || '未检测到 ScreenshotManager 服务'
        : !caps.runtimeReady
          ? caps.notice || 'ScreenshotManager 运行时尚未就绪'
          : !caps.cameraAvailable
            ? caps.notice || '当前场景暂无可用摄影机'
            : '截取当前游戏画面并载入 1:1 裁切器'

  const fileInputRef = React.useRef<HTMLInputElement>(null)
  const imgRef = React.useRef<HTMLImageElement>(null)
  const dragRef = React.useRef<{ pointerId: number; startX: number; startY: number; baseX: number; baseY: number } | null>(
    null,
  )

  const [natural, setNatural] = React.useState<NaturalSize>({ width: 0, height: 0 })
  const [zoom, setZoom] = React.useState(1)
  const [pan, setPan] = React.useState({ x: 0, y: 0 })
  const [dragOver, setDragOver] = React.useState(false)

  React.useEffect(() => {
    setZoom(1)
    setPan({ x: 0, y: 0 })
    setNatural({ width: 0, height: 0 })
    setDragOver(false)
  }, [editor.sourceImageUri, editor.open])

  React.useEffect(() => {
    if (!isOwnerOpen) return
    motionStore.retainDropWatch()
    return () => motionStore.releaseDropWatch()
  }, [isOwnerOpen, capabilities])

  React.useEffect(() => {
    if (isOwnerOpen) motionStore.syncDropTarget()
  }, [isOwnerOpen, editor.saving, editor.capturing, dialogBusy])

  const readLocalFile = (file: File) => {
    if (!file.type.startsWith('image/') && !isSupportedImageExtension(file.name)) {
      motionStore.setCoverEditorError('仅支持拖入图片文件（.png / .jpg / .jpeg / .webp / .bmp）。')
      return
    }
    const reader = new FileReader()
    reader.onload = () => {
      if (typeof reader.result === 'string' && reader.result.startsWith('data:image/')) {
        motionStore.setCoverEditorImageUri(reader.result)
      } else {
        motionStore.setCoverEditorError('无法解析所选图片文件。')
      }
    }
    reader.onerror = () => {
      motionStore.setCoverEditorError('读取所选图片文件失败。')
    }
    reader.readAsDataURL(file)
  }

  const handleDragOver = (event: React.DragEvent<HTMLDivElement>) => {
    event.preventDefault()
    event.stopPropagation()
    if (event.dataTransfer) event.dataTransfer.dropEffect = 'copy'
    if (!dragOver) setDragOver(true)
  }

  const handleDragLeave = (event: React.DragEvent<HTMLDivElement>) => {
    event.preventDefault()
    event.stopPropagation()
    const nextTarget = event.relatedTarget as Node | null
    if (!nextTarget || !event.currentTarget.contains(nextTarget)) {
      setDragOver(false)
    }
  }

  const handleDrop = (event: React.DragEvent<HTMLDivElement>) => {
    event.preventDefault()
    event.stopPropagation()
    setDragOver(false)
    if (editor.saving || editor.capturing) return
    const file = event.dataTransfer?.files?.[0]
    if (file) readLocalFile(file)
  }

  const handlePickImage = () => {
    if (canUseHostFileDialog) {
      void motionStore.browseFiles('cover-image')
    } else {
      fileInputRef.current?.click()
    }
  }

  const handleZoomChange = (nextZoom: number) => {
    const clampedZoom = Math.min(Math.max(nextZoom, 1), 4)
    setZoom(clampedZoom)
    setPan((prev) => clampPan(prev.x, prev.y, natural, clampedZoom))
  }

  const handlePointerDown = (event: React.PointerEvent<HTMLDivElement>) => {
    if (!editor.sourceImageUri || natural.width <= 0) return
    event.currentTarget.setPointerCapture(event.pointerId)
    dragRef.current = {
      pointerId: event.pointerId,
      startX: event.clientX,
      startY: event.clientY,
      baseX: pan.x,
      baseY: pan.y,
    }
  }

  const handlePointerMove = (event: React.PointerEvent<HTMLDivElement>) => {
    const drag = dragRef.current
    if (!drag || drag.pointerId !== event.pointerId) return
    const dx = event.clientX - drag.startX
    const dy = event.clientY - drag.startY
    setPan(clampPan(drag.baseX + dx, drag.baseY + dy, natural, zoom))
  }

  const handlePointerUp = (event: React.PointerEvent<HTMLDivElement>) => {
    if (dragRef.current?.pointerId === event.pointerId) {
      dragRef.current = null
    }
  }

  const handleSave = async () => {
    if (!editor.sourceImageUri) return
    let outputDataUri = editor.sourceImageUri
    const img = imgRef.current
    if (img && natural.width > 0 && natural.height > 0) {
      try {
        const canvas = document.createElement('canvas')
        canvas.width = OUTPUT_SIZE
        canvas.height = OUTPUT_SIZE
        const ctx = canvas.getContext('2d')
        if (ctx) {
          const baseScale = Math.max(CROP_VIEW_SIZE / natural.width, CROP_VIEW_SIZE / natural.height)
          const totalScale = baseScale * zoom
          const scaledW = natural.width * totalScale
          const scaledH = natural.height * totalScale
          const clampedPan = clampPan(pan.x, pan.y, natural, zoom)
          const imgLeft = (CROP_VIEW_SIZE - scaledW) / 2 + clampedPan.x
          const imgTop = (CROP_VIEW_SIZE - scaledH) / 2 + clampedPan.y
          const sx = Math.max(0, -imgLeft / totalScale)
          const sy = Math.max(0, -imgTop / totalScale)
          const sw = Math.min(natural.width - sx, CROP_VIEW_SIZE / totalScale)
          const sh = Math.min(natural.height - sy, CROP_VIEW_SIZE / totalScale)
          ctx.imageSmoothingEnabled = true
          ctx.imageSmoothingQuality = 'high'
          ctx.drawImage(img, sx, sy, sw, sh, 0, 0, OUTPUT_SIZE, OUTPUT_SIZE)
          const encoded = canvas.toDataURL('image/png')
          if (encoded.startsWith('data:image/png')) {
            outputDataUri = encoded
          }
        }
      } catch {
        // Fallback to raw image data URI if canvas export is unavailable (e.g. headless test DOM).
      }
    }
    await motionStore.saveCoverEditor(outputDataUri)
  }

  const baseScale =
    natural.width > 0 && natural.height > 0
      ? Math.max(CROP_VIEW_SIZE / natural.width, CROP_VIEW_SIZE / natural.height)
      : 1
  const scaledW = natural.width * baseScale * zoom
  const scaledH = natural.height * baseScale * zoom
  const clampedPan = clampPan(pan.x, pan.y, natural, zoom)

  const nativeDragActive = isOwnerOpen && dropVisual !== 'none'

  return (
    <Dialog open={isOwnerOpen} onOpenChange={(next) => { if (!next && !editor.saving) motionStore.closeCoverEditor() }}>
      <DialogContent
        className="flex max-h-[85vh] w-[calc(100vw-2rem)] max-w-xl flex-col gap-0 overflow-hidden p-0"
        data-testid="cover-editor-modal"
        onDragEnter={handleDragOver}
        onDragOver={handleDragOver}
        onDragLeave={handleDragLeave}
        onDrop={handleDrop}
      >
        <DialogHeader className="shrink-0 border-b border-border/60 px-5 pt-5 pb-3 pr-10">
          <DialogTitle>编辑曲目封面（1:1 正方形）</DialogTitle>
          <DialogDescription className="break-all [overflow-wrap:anywhere]">
            {editor.trackTitle ? `目标曲目：${editor.trackTitle}` : '选择本地图片或从 ScreenshotManager 实时拍照，拖动并缩放裁切 1:1 封面。'}
          </DialogDescription>
        </DialogHeader>

        <div className="min-h-0 flex-1 space-y-4 overflow-y-auto px-5 py-4">
          <input
            ref={fileInputRef}
            type="file"
            accept=".png,.jpg,.jpeg,.webp,.bmp,image/png,image/jpeg,image/webp,image/bmp"
            className="hidden"
            data-testid="cover-file-input"
            onChange={(event) => {
              const file = event.target.files?.[0]
              if (file) readLocalFile(file)
              event.target.value = ''
            }}
          />

          <div className="flex flex-wrap items-center gap-2">
            <Button
              type="button"
              variant="outline"
              size="sm"
              disabled={editor.saving || editor.capturing || dialogBusy}
              onClick={handlePickImage}
              data-testid="cover-pick-file-btn"
            >
              <RiImageAddFill className="size-4" />
              选择本地图片…
            </Button>

            <Button
              type="button"
              variant="outline"
              size="sm"
              disabled={!canCaptureScreenshot}
              title={screenshotTooltip}
              onClick={() => void motionStore.captureScreenshotForCover()}
              data-testid="cover-capture-screenshot-btn"
            >
              {editor.capturing ? (
                <RiLoader4Line className="size-4 animate-spin" />
              ) : (
                <RiCameraFill className="size-4" />
              )}
              {editor.capturing ? '拍照中…' : '从 ScreenshotManager 拍照'}
            </Button>

            {editor.sourceImageUri && (
              <Button
                type="button"
                variant="ghost"
                size="sm"
                className="ml-auto text-destructive hover:bg-destructive/10 hover:text-destructive"
                disabled={editor.saving || editor.capturing}
                onClick={() => void motionStore.clearCoverFromEditor()}
                data-testid="cover-clear-btn"
              >
                <RiDeleteBinLine className="size-4" />
                恢复默认封面
              </Button>
            )}
          </div>

          {!canCaptureScreenshot && screenshotTooltip && (
            <p className="text-xs text-muted-foreground" data-testid="cover-screenshot-notice">
              拍照联动：{screenshotTooltip}
            </p>
          )}

          {editor.error && (
            <Alert variant="destructive">
              <AlertDescription className="break-all [overflow-wrap:anywhere]">{editor.error}</AlertDescription>
            </Alert>
          )}

          <div className="flex flex-col items-center gap-4 sm:flex-row sm:items-start">
            {/* 1:1 Interactive Square Cropper */}
            <div className="flex flex-col items-center gap-2.5">
              <div
                data-testid="cover-crop-canvas"
                onPointerDown={handlePointerDown}
                onPointerMove={handlePointerMove}
                onPointerUp={handlePointerUp}
                onPointerCancel={handlePointerUp}
                onWheel={(event) => {
                  if (!editor.sourceImageUri) return
                  event.preventDefault()
                  handleZoomChange(zoom + (event.deltaY < 0 ? 0.12 : -0.12))
                }}
                onDragEnter={handleDragOver}
                onDragOver={handleDragOver}
                onDragLeave={handleDragLeave}
                onDrop={handleDrop}
                style={{ width: CROP_VIEW_SIZE, height: CROP_VIEW_SIZE }}
                className={cn(
                  'relative shrink-0 select-none overflow-hidden rounded-lg border-2 bg-muted/40',
                  editor.sourceImageUri ? 'cursor-grab active:cursor-grabbing' : 'cursor-default',
                  dragOver || nativeDragActive ? 'border-primary ring-2 ring-primary/30' : 'border-border',
                )}
              >
                {editor.sourceImageUri ? (
                  <>
                    <img
                      ref={imgRef}
                      src={editor.sourceImageUri}
                      alt="封面裁切预览"
                      draggable={false}
                      onLoad={(event) => {
                        const img = event.currentTarget
                        setNatural({ width: img.naturalWidth || 512, height: img.naturalHeight || 512 })
                        setPan({ x: 0, y: 0 })
                      }}
                      style={
                        natural.width > 0
                          ? {
                              width: scaledW,
                              height: scaledH,
                              left: (CROP_VIEW_SIZE - scaledW) / 2 + clampedPan.x,
                              top: (CROP_VIEW_SIZE - scaledH) / 2 + clampedPan.y,
                            }
                          : {
                              width: '100%',
                              height: '100%',
                              left: 0,
                              top: 0,
                              objectFit: 'cover',
                            }
                      }
                      className="pointer-events-none absolute max-w-none"
                    />
                    {/* 1:1 rule-of-thirds overlay */}
                    <div aria-hidden="true" className="pointer-events-none absolute inset-0 border border-white/25">
                      <div className="absolute inset-y-0 left-1/3 w-px bg-white/20" />
                      <div className="absolute inset-y-0 left-2/3 w-px bg-white/20" />
                      <div className="absolute inset-x-0 top-1/3 h-px bg-white/20" />
                      <div className="absolute inset-x-0 top-2/3 h-px bg-white/20" />
                    </div>
                  </>
                ) : (
                  <div className="flex size-full flex-col items-center justify-center gap-2 p-4 text-center text-muted-foreground">
                    <div className="size-20 overflow-hidden rounded-md border border-border/60">
                      <DefaultAlbumCover />
                    </div>
                    <p className="text-xs">拖入图片到此处，或点击上方“选择本地图片” / “拍照”</p>
                  </div>
                )}
              </div>

              {editor.sourceImageUri && (
                <div className="flex w-full max-w-[272px] items-center gap-2">
                  <RiZoomInLine className="size-4 shrink-0 text-muted-foreground" />
                  <Slider
                    aria-label="封面缩放"
                    value={[zoom]}
                    min={1}
                    max={4}
                    step={0.02}
                    onValueChange={([v]) => handleZoomChange(v)}
                  />
                  <span className="w-11 shrink-0 text-right text-xs tabular-nums text-muted-foreground">
                    {zoom.toFixed(2)}x
                  </span>
                  <Button
                    type="button"
                    variant="ghost"
                    size="icon"
                    className="size-7 shrink-0"
                    title="重置位置与缩放"
                    aria-label="重置裁切"
                    onClick={() => {
                      setZoom(1)
                      setPan({ x: 0, y: 0 })
                    }}
                  >
                    <RiRefreshLine className="size-3.5" />
                  </Button>
                </div>
              )}
            </div>

            {/* Live square previews */}
            <div className="flex flex-1 flex-col gap-3 rounded-lg border border-border/70 bg-muted/25 p-3">
              <div className="text-sm font-medium text-foreground">实时 1:1 预览</div>
              <div className="flex items-end gap-3">
                <div className="space-y-1">
                  <div className="relative size-20 overflow-hidden rounded-md border border-border/70 bg-muted">
                    {editor.sourceImageUri ? (
                      <div
                        style={{
                          width: CROP_VIEW_SIZE,
                          height: CROP_VIEW_SIZE,
                          transform: `scale(${80 / CROP_VIEW_SIZE})`,
                          transformOrigin: 'top left',
                        }}
                        className="relative"
                      >
                        <img
                          src={editor.sourceImageUri}
                          alt=""
                          style={
                            natural.width > 0
                              ? {
                                  width: scaledW,
                                  height: scaledH,
                                  left: (CROP_VIEW_SIZE - scaledW) / 2 + clampedPan.x,
                                  top: (CROP_VIEW_SIZE - scaledH) / 2 + clampedPan.y,
                                }
                              : { width: '100%', height: '100%', objectFit: 'cover' }
                          }
                          className="pointer-events-none absolute max-w-none"
                        />
                      </div>
                    ) : (
                      <DefaultAlbumCover />
                    )}
                  </div>
                  <div className="text-micro text-muted-foreground">曲目封面 (1:1)</div>
                </div>

                <div className="space-y-1">
                  <div className="relative size-8 overflow-hidden rounded border border-border/70 bg-muted">
                    {editor.sourceImageUri ? (
                      <div
                        style={{
                          width: CROP_VIEW_SIZE,
                          height: CROP_VIEW_SIZE,
                          transform: `scale(${32 / CROP_VIEW_SIZE})`,
                          transformOrigin: 'top left',
                        }}
                        className="relative"
                      >
                        <img
                          src={editor.sourceImageUri}
                          alt=""
                          style={
                            natural.width > 0
                              ? {
                                  width: scaledW,
                                  height: scaledH,
                                  left: (CROP_VIEW_SIZE - scaledW) / 2 + clampedPan.x,
                                  top: (CROP_VIEW_SIZE - scaledH) / 2 + clampedPan.y,
                                }
                              : { width: '100%', height: '100%', objectFit: 'cover' }
                          }
                          className="pointer-events-none absolute max-w-none"
                        />
                      </div>
                    ) : (
                      <DefaultAlbumCover />
                    )}
                  </div>
                  <div className="text-micro text-muted-foreground">列表图标</div>
                </div>
              </div>
              <p className="text-xs leading-relaxed text-muted-foreground">
                固定 1:1 正方形裁切，保存后将输出 512×512 PNG（<code className="text-micro">cover.png</code>）到曲目目录。
              </p>
            </div>
          </div>
        </div>

        <DialogFooter className="shrink-0 border-t border-border/60 bg-card px-5 py-3.5">
          <Button
            type="button"
            variant="outline"
            disabled={editor.saving}
            onClick={() => motionStore.closeCoverEditor()}
          >
            取消
          </Button>
          <Button
            type="button"
            disabled={!editor.sourceImageUri || editor.saving || editor.capturing}
            onClick={() => void handleSave()}
            data-testid="cover-save-btn"
          >
            {editor.saving ? '正在保存…' : '保存封面'}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  )
}
