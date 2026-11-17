import { Button } from '@/components/ui/button'
import { Dialog, DialogContent, DialogHeader, DialogTitle } from '@/components/ui/dialog'
import { Progress } from '@/components/ui/progress'
import { motionStore, useMotionStore } from '../store'

// Global import progress modal (fix-issue-5 semantics): visible across pages,
// 550ms minimum display, 250ms stale-success guard, error detail box.
export function ImportProgressModal() {
  const ui = useMotionStore((state) => state.importUi)
  const open = ui.visible || ui.errorModalOpen
  if (!open) return null
  const isError = ui.errorModalOpen
  const percent = Math.round(ui.displayProgress * 100)
  return (
    <Dialog open={open} onOpenChange={(next) => { if (!next && isError) motionStore.closeImportError() }}>
      <DialogContent className="max-w-lg" data-testid="import-progress-modal">
        <DialogHeader>
          <DialogTitle>{isError ? '导入失败' : '导入曲目中'}</DialogTitle>
        </DialogHeader>
        {isError ? (
          <div className="space-y-3">
            <p className="text-sm text-destructive">导入遇到错误，曲目未导入或保持原状。</p>
            <pre className="max-h-40 overflow-auto rounded border bg-muted p-2 text-xs whitespace-pre-wrap">
              {ui.errorDetail || '无详细信息'}
            </pre>
            <Button size="sm" onClick={() => motionStore.closeImportError()}>关闭</Button>
          </div>
        ) : (
          <div className="space-y-3">
            <Progress value={percent} />
            <p className="text-sm text-muted-foreground">{percent}%{ui.indeterminate ? '（不定进度）' : ''}</p>
            <p className="text-xs text-muted-foreground">{ui.displayStatus || '正在处理...'}</p>
          </div>
        )}
      </DialogContent>
    </Dialog>
  )
}
