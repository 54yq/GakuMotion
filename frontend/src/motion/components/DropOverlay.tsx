import { useMotionStore } from '../store'

export function DropOverlay() {
  const drop = useMotionStore((state) => state.drop)
  if (drop.visual === 'none') return null
  const isHover = drop.visual === 'hover'
  const isInvalid = drop.visual === 'invalid'
  return (
    <div
      className={`pointer-events-none absolute inset-2 z-40 flex flex-col items-center justify-center gap-1 rounded-lg border-2 border-dashed ${
        isHover ? 'border-primary bg-primary/10' : isInvalid ? 'border-destructive bg-destructive/10' : 'border-muted-foreground/40 bg-muted/20'
      }`}
      data-testid="drop-overlay"
    >
      <p className="text-sm font-medium">
        {isInvalid ? '没有可导入的有效文件' : isHover ? `松开导入 ${drop.accepted} 个文件` : '拖放文件到此处导入曲库'}
      </p>
      {drop.fileCount > 0 && <p className="text-xs text-muted-foreground">共 {drop.fileCount} 个文件</p>}
      {(drop.hostElevated || drop.hostBlocked) && (
        <p className="max-w-md px-6 text-center text-xs text-muted-foreground">
          {drop.hostElevated
            ? '游戏以管理员权限运行，Explorer 无法直接传递拖放预览；松开后仍会尝试导入。'
            : '当前宿主不支持拖放悬停预览，松开后仍会尝试导入。'}
        </p>
      )}
    </div>
  )
}
