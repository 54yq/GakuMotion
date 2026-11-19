import * as React from 'react'
import { cn } from '@/lib/utils'

interface PopoverContextValue {
  open: boolean
  setOpen: (next: boolean) => void
  containerRef: React.RefObject<HTMLDivElement | null>
}

const PopoverContext = React.createContext<PopoverContextValue | null>(null)

function usePopoverContext(): PopoverContextValue {
  const ctx = React.useContext(PopoverContext)
  if (!ctx) throw new Error('Popover components must be used inside <Popover>')
  return ctx
}

export interface PopoverProps {
  open?: boolean
  defaultOpen?: boolean
  onOpenChange?: (open: boolean) => void
  className?: string
  children: React.ReactNode
}

/**
 * Anchor-relative popover compatible with WebView2 CSS `zoom` and Radix Portal overlays.
 */
export function Popover({ open: controlledOpen, defaultOpen = false, onOpenChange, className, children }: PopoverProps) {
  const [uncontrolledOpen, setUncontrolledOpen] = React.useState(defaultOpen)
  const isControlled = controlledOpen !== undefined
  const open = isControlled ? controlledOpen : uncontrolledOpen
  const containerRef = React.useRef<HTMLDivElement | null>(null)

  const setOpen = React.useCallback(
    (next: boolean) => {
      if (!isControlled) setUncontrolledOpen(next)
      onOpenChange?.(next)
    },
    [isControlled, onOpenChange],
  )

  return (
    <PopoverContext.Provider value={{ open, setOpen, containerRef }}>
      <div ref={containerRef} className={cn('relative inline-flex', className)}>
        {children}
      </div>
    </PopoverContext.Provider>
  )
}

export interface PopoverTriggerProps extends React.ButtonHTMLAttributes<HTMLButtonElement> {
  asChild?: boolean
}

export const PopoverTrigger = React.forwardRef<HTMLButtonElement, PopoverTriggerProps>(
  ({ asChild = false, onClick, children, ...props }, ref) => {
    const { open, setOpen } = usePopoverContext()

    const handleClick = (event: React.MouseEvent<HTMLButtonElement>) => {
      onClick?.(event)
      if (!event.defaultPrevented) setOpen(!open)
    }

    if (asChild && React.isValidElement(children)) {
      const child = children as React.ReactElement<Record<string, unknown>>
      const childOnClick = child.props.onClick as ((e: React.MouseEvent<HTMLButtonElement>) => void) | undefined
      return React.cloneElement(child, {
        ...props,
        'aria-expanded': open,
        'data-state': open ? 'open' : 'closed',
        onClick: (event: React.MouseEvent<HTMLButtonElement>) => {
          childOnClick?.(event)
          handleClick(event)
        },
      })
    }

    return (
      <button
        ref={ref}
        type="button"
        aria-expanded={open}
        data-state={open ? 'open' : 'closed'}
        onClick={handleClick}
        {...props}
      >
        {children}
      </button>
    )
  },
)
PopoverTrigger.displayName = 'PopoverTrigger'

export interface PopoverContentProps extends React.HTMLAttributes<HTMLDivElement> {
  side?: 'top' | 'bottom'
  align?: 'start' | 'center' | 'end'
  pinned?: boolean
}

function isProtectedOverlayTarget(target: Node): boolean {
  if (!(target instanceof Element)) return false
  return (
    target.closest('[data-radix-popper-content-wrapper]') !== null ||
    target.closest('[role="listbox"]') !== null ||
    target.closest('[data-popover-ignore-outside]') !== null
  )
}

export const PopoverContent = React.forwardRef<HTMLDivElement, PopoverContentProps>(
  ({ side = 'top', align = 'end', pinned = false, className, children, ...props }, ref) => {
    const { open, setOpen, containerRef } = usePopoverContext()

    React.useEffect(() => {
      if (!open || pinned) return
      const onPointerDown = (event: PointerEvent) => {
        const target = event.target as Node | null
        if (!target) return
        if (containerRef.current?.contains(target)) return
        if (isProtectedOverlayTarget(target)) return
        setOpen(false)
      }
      const onKeyDown = (event: KeyboardEvent) => {
        if (event.key === 'Escape') {
          event.stopPropagation()
          setOpen(false)
        }
      }
      document.addEventListener('pointerdown', onPointerDown, true)
      document.addEventListener('keydown', onKeyDown)
      return () => {
        document.removeEventListener('pointerdown', onPointerDown, true)
        document.removeEventListener('keydown', onKeyDown)
      }
    }, [open, pinned, setOpen, containerRef])

    if (!open) return null

    const sideClass = side === 'top' ? 'bottom-full mb-2' : 'top-full mt-2'
    const alignClass =
      align === 'start' ? 'left-0' : align === 'center' ? 'left-1/2 -translate-x-1/2' : 'right-0'

    return (
      <div
        ref={ref}
        role="dialog"
        data-state="open"
        className={cn(
          'absolute z-50 rounded-lg border bg-popover p-3 text-popover-foreground shadow-lg animate-fade-in',
          sideClass,
          alignClass,
          className,
        )}
        {...props}
      >
        {children}
      </div>
    )
  },
)
PopoverContent.displayName = 'PopoverContent'
