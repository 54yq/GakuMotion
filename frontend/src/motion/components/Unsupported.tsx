import { Construction } from 'lucide-react'
import { Badge } from '@/components/ui/badge'
import { cn } from '@/lib/utils'

// Marks a control whose native bridge method is not implemented yet. The UI
// never pretends success for these: controls stay disabled and labeled.
export function PendingBadge({ className, label = '待迁移' }: { className?: string; label?: string }) {
  return (
    <Badge
      variant="outline"
      className={cn('border-warning/60 bg-warning/10 text-warning text-[10px] px-1.5 py-0', className)}
      title="原生方法尚未实现，功能不可用"
    >
      {label}
    </Badge>
  )
}

export function UnsupportedPanel({
  title,
  description,
  methods,
  className,
}: {
  title: string
  description?: string
  methods?: string[]
  className?: string
}) {
  return (
    <div className={cn('rounded-md border border-dashed border-border bg-muted/30 p-4', className)} data-testid="unsupported-panel">
      <div className="flex items-center gap-2">
        <Construction className="size-4 text-warning" />
        <span className="text-sm font-medium">{title}</span>
        <PendingBadge label="待迁移 / 不可用" />
      </div>
      {description && <p className="mt-2 text-sm text-muted-foreground">{description}</p>}
      {methods && methods.length > 0 && (
        <p className="mt-2 text-xs text-muted-foreground">
          等待原生方法：<code className="font-mono">{methods.join('、')}</code>
        </p>
      )}
    </div>
  )
}
