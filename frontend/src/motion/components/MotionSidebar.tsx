import {
  RiCameraFill,
  RiFlaskFill,
  RiMusic2Fill,
  RiPlayList2Fill,
  RiSettings3Fill,
  RiSparkling2Fill,
  type RemixiconComponentType,
} from '@remixicon/react'
import wordmarkDark from '../../assets/gakumotion-wordmark-dark.svg'
import wordmarkLight from '../../assets/gakumotion-wordmark-light.svg'
import { Button } from '@/components/ui/button'
import { cn } from '@/lib/utils'
import { motionStore, useMotionStore, type MotionPage } from '../store'

const PRIMARY_NAV: Array<{ page: MotionPage; label: string; icon: RemixiconComponentType }> = [
  { page: 'library', label: '曲库', icon: RiPlayList2Fill },
  { page: 'motion', label: '动作', icon: RiMusic2Fill },
  { page: 'camera', label: '镜头', icon: RiCameraFill },
  { page: 'stage', label: '舞台', icon: RiSparkling2Fill },
  { page: 'external-stage', label: '外部舞台', icon: RiSparkling2Fill },
]

export function MotionSidebar({ onNavigate }: { onNavigate?: () => void }) {
  const activePage = useMotionStore((state) => state.page)
  const developerMode = useMotionStore((state) => state.developerMode)
  const libraryCount = useMotionStore((state) => state.library.entries.length)
  const theme = useMotionStore((state) => state.appearance?.theme ?? 0)
  const ensembleActive = useMotionStore((state) => state.ensemble.active)
  const isLightTheme = theme === 2 || theme === 3

  const renderItem = (page: MotionPage, label: string, Icon: RemixiconComponentType, badgeText?: string) => {
    const active = activePage === page
    return (
      <Button
        key={page}
        variant="ghost"
        aria-label={label}
        aria-current={active ? 'page' : undefined}
        onClick={() => { motionStore.setPage(page); onNavigate?.() }}
        className={cn(
          'relative h-8 w-full justify-start gap-2.5 rounded-md px-2.5 text-[13px] transition-colors',
          active
            ? 'bg-sidebar-accent font-semibold text-sidebar-accent-foreground'
            : 'font-normal text-sidebar-foreground/85 hover:bg-foreground/[0.06] hover:text-sidebar-foreground',
        )}
      >
        {active && (
          <span
            aria-hidden="true"
            className="absolute left-0 top-1.5 bottom-1.5 w-[3px] rounded-r bg-primary"
          />
        )}
        <Icon className={cn('size-4 shrink-0', active ? 'text-primary' : 'text-muted-foreground')} />
        <span className="truncate">{label}</span>
        {badgeText && (
          <span
            aria-hidden="true"
            className={cn(
              'ml-auto rounded-[4px] px-1.5 py-0.5 text-[10px] font-medium leading-none tabular-nums',
              active ? 'bg-primary/15 text-primary' : 'text-muted-foreground',
            )}
          >
            {badgeText}
          </span>
        )}
      </Button>
    )
  }

  return (
    <nav
      className="flex w-[160px] shrink-0 flex-col bg-sidebar px-2 py-2.5"
      aria-label="主导航"
      data-testid="motion-sidebar"
    >
      {/* Brand header — original ImGui Light / Dark wordmark */}
      <div className="mb-2.5 flex items-center px-1.5 pt-0.5">
        <img
          src={isLightTheme ? wordmarkLight : wordmarkDark}
          alt="GakuMotion"
          className="h-10 w-auto select-none object-contain"
          draggable={false}
        />
      </div>

      {/* Music & Stage workspace */}
      <div className="px-2 pb-1 text-[10px] font-medium tracking-wider text-muted-foreground/80">
        演出工作区
      </div>
      <div className="flex flex-col gap-0.5">
        {PRIMARY_NAV.map((item) =>
          renderItem(
            item.page,
            item.label,
            item.icon,
            item.page === 'library' && libraryCount > 0
              ? String(libraryCount)
              : item.page === 'motion' && ensembleActive
                ? '3P'
                : undefined,
          ),
        )}
      </div>

      {/* System & developer bottom rail */}
      <div className="mt-auto space-y-0.5 pt-2">
        {renderItem('settings', '设置', RiSettings3Fill)}
        {developerMode && renderItem('developer', '[开发]', RiFlaskFill)}
      </div>
    </nav>
  )
}
