import { Camera, FlaskConical, Library, Music, Settings, Sparkles } from 'lucide-react'
import { Button } from '@/components/ui/button'
import { Separator } from '@/components/ui/separator'
import { cn } from '@/lib/utils'
import { motionStore, useMotionStore, type MotionPage } from '../store'

const NAV_ITEMS: Array<{ page: MotionPage; label: string; icon: typeof Library }> = [
  { page: 'library', label: '曲库', icon: Library },
  { page: 'motion', label: '动作', icon: Music },
  { page: 'camera', label: '镜头', icon: Camera },
  { page: 'stage', label: '舞台', icon: Sparkles },
  { page: 'settings', label: '设置', icon: Settings },
]

export function MotionSidebar() {
  const activePage = useMotionStore((state) => state.page)
  const developerMode = useMotionStore((state) => state.developerMode)

  const renderItem = (page: MotionPage, label: string, Icon: typeof Library) => (
    <Button
      key={page}
      variant="ghost"
      className={cn(
        'w-full justify-start gap-2 text-sidebar-foreground',
        activePage === page && 'bg-sidebar-accent text-sidebar-accent-foreground',
      )}
      aria-current={activePage === page ? 'page' : undefined}
      onClick={() => motionStore.setPage(page)}
    >
      <Icon className="size-4" />
      {label}
    </Button>
  )

  return (
    <nav className="flex w-[130px] shrink-0 flex-col gap-1 border-r bg-sidebar p-2" data-testid="motion-sidebar">
      <div className="px-2 pb-1 pt-1">
        <div className="text-sm font-semibold text-sidebar-foreground">GakuMotion</div>
        <div className="text-[10px] text-muted-foreground">Tech Preview</div>
      </div>
      <Separator className="mb-1 bg-sidebar-border" />
      {NAV_ITEMS.map((item) => renderItem(item.page, item.label, item.icon))}
      {developerMode && (
        <>
          <Separator className="my-1 bg-sidebar-border" />
          {renderItem('developer', '[开发]', FlaskConical)}
        </>
      )}
    </nav>
  )
}
