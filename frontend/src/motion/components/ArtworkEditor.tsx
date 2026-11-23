import { useEffect, useState, type ReactNode } from 'react'
import { Button } from '@/components/ui/button'
import { Dialog, DialogContent, DialogDescription, DialogHeader, DialogTitle, DialogTrigger } from '@/components/ui/dialog'
import { saveArtwork, useArtwork } from '../panelMedia'

interface Crop {
  zoom: number
  x: number
  y: number
}

const centerCrop: Crop = { zoom: 1, x: 50, y: 50 }

function drawCrop(image: ImageBitmap, crop: Crop, canvas: HTMLCanvasElement) {
  const side = Math.min(image.width, image.height) / crop.zoom
  const x = (image.width - side) * crop.x / 100
  const y = (image.height - side) * crop.y / 100
  canvas.getContext('2d')?.drawImage(image, x, y, side, side, 0, 0, canvas.width, canvas.height)
}

export function ArtworkEditor({ artworkKey, label, fallback }: { artworkKey: string; label: string; fallback: ReactNode }) {
  const url = useArtwork(artworkKey)
  const [open, setOpen] = useState(false)
  const [image, setImage] = useState<ImageBitmap>()
  const [crop, setCrop] = useState(centerCrop)
  const [canvas, setCanvas] = useState<HTMLCanvasElement | null>(null)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState('')
  useEffect(() => () => image?.close(), [image])
  useEffect(() => {
    if (image && canvas) {
      canvas.getContext('2d')?.clearRect(0, 0, canvas.width, canvas.height)
      drawCrop(image, crop, canvas)
    }
  }, [image, crop, canvas])

  const selectFile = async (file?: File) => {
    if (!file) return
    setError('')
    if (file.size > 20 * 1024 * 1024) {
      setError('请选择小于 20 MB 的图片。')
      return
    }
    setBusy(true)
    try {
      setImage(await createImageBitmap(file))
      setCrop(centerCrop)
    } catch {
      setError('无法读取这张图片。')
    } finally {
      setBusy(false)
    }
  }

  const save = async (remove = false) => {
    setBusy(true)
    setError('')
    try {
      let blob: Blob | undefined
      if (!remove) {
        if (!canvas) return
        blob = await new Promise<Blob>((resolve, reject) => canvas.toBlob(
          (value) => value ? resolve(value) : reject(new Error('encode')),
          'image/webp', 0.9,
        ))
      }
      await saveArtwork(artworkKey, blob)
      setImage(undefined)
      setOpen(false)
    } catch {
      setError('无法保存图片，请检查本地存储。')
    } finally {
      setBusy(false)
    }
  }

  return (
    <Dialog open={open} onOpenChange={(next) => {
      if (busy) return
      setOpen(next)
      if (!next) {
        setImage(undefined)
        setError('')
      }
    }}>
      <DialogTrigger asChild>
        <button type="button" aria-label={label} title={label}
          className="flex size-9 shrink-0 items-center justify-center overflow-hidden rounded-md border bg-muted text-muted-foreground hover:border-primary">
          {url ? <img src={url} alt="" className="size-full object-cover" /> : fallback}
        </button>
      </DialogTrigger>
      <DialogContent>
        <DialogHeader>
          <DialogTitle>{label}</DialogTitle>
          <DialogDescription>图片保存在当前 Web 看板的本地存储中。</DialogDescription>
        </DialogHeader>
        <input aria-label="选择图片" type="file" accept="image/*" disabled={busy}
          onChange={(event) => void selectFile(event.currentTarget.files?.[0])} />
        {image && (
          <div className="space-y-2">
            <canvas ref={setCanvas} width={384} height={384} className="mx-auto aspect-square w-40 rounded-md bg-muted" />
            {([
              ['zoom', '缩放', 1, 3, 0.01],
              ['x', '水平位置', 0, 100, 1],
              ['y', '垂直位置', 0, 100, 1],
            ] as const).map(([field, title, min, max, step]) => (
              <label key={field} className="flex items-center gap-3 text-sm">
                <span className="w-20">{title}</span>
                <input className="flex-1" type="range" min={min} max={max} step={step} value={crop[field]}
                  disabled={busy} onChange={(event) => setCrop({ ...crop, [field]: event.currentTarget.valueAsNumber })} />
              </label>
            ))}
          </div>
        )}
        {error && <p role="alert" className="text-sm text-destructive">{error}</p>}
        <div className="flex justify-end gap-2">
          {url && <Button variant="outline" disabled={busy} onClick={() => void save(true)}>移除图片</Button>}
          <Button disabled={!image || busy} onClick={() => void save()}>保存裁剪</Button>
        </div>
      </DialogContent>
    </Dialog>
  )
}
