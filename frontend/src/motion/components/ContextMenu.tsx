import * as React from 'react'
import { cn } from '@/lib/utils'

export interface ContextMenuItem {
  id: string
  label: string
  icon?: React.ReactNode
  shortcut?: string
  destructive?: boolean
  disabled?: boolean
  onSelect: () => void
}

export type ContextMenuEntry = ContextMenuItem | { type: 'separator'; id: string }

export interface ContextMenuProps {
  open: boolean
  x: number
  y: number
  items: ContextMenuEntry[]
  onClose: () => void
  testId?: string
}

/**
 * Lightweight desktop context menu accounting for document CSS zoom and viewport bounds.
 */
export function ContextMenu({ open, x, y, items, onClose, testId = 'context-menu' }: ContextMenuProps) {
  const menuRef = React.useRef<HTMLDivElement>(null)
  const [pos, setPos] = React.useState({ left: x, top: y })

  React.useLayoutEffect(() => {
    if (!open) return
    const zoom = Number(document.documentElement.style.zoom || '1') || 1
    let nextLeft = x / zoom
    let nextTop = y / zoom
    const rect = menuRef.current?.getBoundingClientRect()
    const vw = window.innerWidth / zoom
    const vh = window.innerHeight / zoom
    if (rect) {
      const mw = rect.width / zoom
      const mh = rect.height / zoom
      if (nextLeft + mw > vw - 8) nextLeft = Math.max(8, vw - mw - 8)
      if (nextTop + mh > vh - 8) nextTop = Math.max(8, vh - mh - 8)
    }
    setPos({ left: nextLeft, top: nextTop })
  }, [open, x, y])

  React.useEffect(() => {
    if (!open) return
    const onPointerDown = (event: PointerEvent) => {
      if (menuRef.current && event.target instanceof Node && menuRef.current.contains(event.target)) return
      onClose()
    }
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        event.stopPropagation()
        onClose()
      }
    }
    const onScroll = () => onClose()
    document.addEventListener('pointerdown', onPointerDown, true)
    document.addEventListener('keydown', onKeyDown)
    window.addEventListener('scroll', onScroll, true)
    return () => {
      document.removeEventListener('pointerdown', onPointerDown, true)
      document.removeEventListener('keydown', onKeyDown)
      window.removeEventListener('scroll', onScroll, true)
    }
  }, [open, onClose])

  if (!open) return null

  return (
    <div
      ref={menuRef}
      role="menu"
      data-testid={testId}
      style={{ left: `${pos.left}px`, top: `${pos.top}px` }}
      className="fixed z-50 min-w-[184px] rounded-lg border bg-popover p-1 text-popover-foreground shadow-lg animate-fade-in"
    >
      {items.map((entry) => {
        if ('type' in entry && entry.type === 'separator') {
          return <div key={entry.id} role="separator" className="my-1 h-px bg-border/70" />
        }
        const item = entry as ContextMenuItem
        return (
          <button
            key={item.id}
            type="button"
            role="menuitem"
            disabled={item.disabled}
            onClick={() => {
              onClose()
              item.onSelect()
            }}
            className={cn(
              'flex w-full items-center gap-2 rounded px-2.5 py-1.5 text-left text-xs transition-colors focus-visible:outline-none focus-visible:bg-accent disabled:pointer-events-none disabled:opacity-45',
              item.destructive
                ? 'text-destructive hover:bg-destructive/12'
                : 'text-popover-foreground hover:bg-accent hover:text-accent-foreground',
            )}
          >
            {item.icon && <span className="shrink-0 opacity-80">{item.icon}</span>}
            <span className="flex-1 truncate">{item.label}</span>
            {item.shortcut && <span className="ml-2 text-[10px] text-muted-foreground">{item.shortcut}</span>}
          </button>
        )
      })}
    </div>
  )
}
