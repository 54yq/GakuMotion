import * as React from 'react'
import { cn } from '@/lib/utils'

export interface DragFloatProps {
  value: number
  min: number
  max: number
  /** Step increment for keyboard nudging and default sensitivity fallback. */
  step?: number
  /** Units changed per horizontal pixel dragged (ImGui v_speed equivalent). Defaults to `step`. */
  dragSpeed?: number
  /** Pixels of mouse travel per one `dragSpeed` increment. Defaults to `2`. */
  pixelsPerStep?: number
  decimals?: number
  /** Minimum interval (ms) between live `onChange` updates while dragging. */
  throttleMs?: number
  disabled?: boolean
  label?: string
  unit?: string
  ariaLabel: string
  className?: string
  inputClassName?: string
  /** Throttled live preview callback invoked while dragging. */
  onChange?: (value: number) => void
  /** Final commit callback invoked on drag release, Enter, blur, or keyboard nudge. */
  onCommit: (value: number) => void
}

function clampAndRound(raw: number, min: number, max: number, decimals: number): number {
  const clamped = Math.min(Math.max(raw, min), max)
  const factor = Math.pow(10, decimals)
  const rounded = Math.round(clamped * factor) / factor
  return Object.is(rounded, -0) ? 0 : rounded
}

/**
 * ImGui-inspired DragFloat numeric input:
 * - Suppresses native browser spinner arrows (`type="text"` + `inputMode="decimal"`).
 * - Horizontal pointer drag works even after leaving the input bounds via window listeners.
 * - Real-time local preview + throttled `onChange` + guaranteed `onCommit` on release.
 * - Click (<3px movement) enters text-editing mode with Enter/Blur commit and Escape cancel.
 */
export function DragFloat({
  value,
  min,
  max,
  step = 0.01,
  dragSpeed,
  pixelsPerStep = 2,
  decimals = 2,
  throttleMs = 48,
  disabled,
  label,
  unit,
  ariaLabel,
  className,
  inputClassName,
  onChange,
  onCommit,
}: DragFloatProps) {
  const effectiveSpeed = dragSpeed ?? step
  const [editing, setEditing] = React.useState(false)
  const [dragging, setDragging] = React.useState(false)
  const [draftText, setDraftText] = React.useState(value.toFixed(decimals))
  const [previewValue, setPreviewValue] = React.useState<number | null>(null)

  const inputRef = React.useRef<HTMLInputElement>(null)
  const editingRef = React.useRef(false)
  const onChangeRef = React.useRef(onChange)
  const onCommitRef = React.useRef(onCommit)
  onChangeRef.current = onChange
  onCommitRef.current = onCommit

  const throttleTimerRef = React.useRef<ReturnType<typeof setTimeout> | null>(null)
  const pendingLiveRef = React.useRef<number | null>(null)
  const lastDispatchedLiveRef = React.useRef<number | null>(null)

  const clearThrottle = React.useCallback(() => {
    if (throttleTimerRef.current !== null) {
      clearTimeout(throttleTimerRef.current)
      throttleTimerRef.current = null
    }
    pendingLiveRef.current = null
  }, [])

  React.useEffect(() => {
    return () => clearThrottle()
  }, [clearThrottle])

  React.useEffect(() => {
    if (!editing && !dragging) {
      setDraftText(value.toFixed(decimals))
      setPreviewValue(null)
    }
  }, [value, decimals, editing, dragging])

  const emitThrottledLive = React.useCallback(
    (next: number) => {
      if (!onChangeRef.current) return
      if (lastDispatchedLiveRef.current === next) return
      pendingLiveRef.current = next
      if (throttleTimerRef.current !== null) return
      lastDispatchedLiveRef.current = next
      onChangeRef.current(next)
      throttleTimerRef.current = setTimeout(() => {
        throttleTimerRef.current = null
        const queued = pendingLiveRef.current
        pendingLiveRef.current = null
        if (queued !== null && queued !== lastDispatchedLiveRef.current) {
          lastDispatchedLiveRef.current = queued
          onChangeRef.current?.(queued)
        }
      }, throttleMs)
    },
    [throttleMs],
  )

  const commitText = () => {
    editingRef.current = false
    setEditing(false)
    const parsed = Number(draftText.trim())
    if (!Number.isFinite(parsed)) {
      setDraftText(value.toFixed(decimals))
      return
    }
    const clamped = clampAndRound(parsed, min, max, decimals)
    setDraftText(clamped.toFixed(decimals))
    if (clamped !== value) onCommitRef.current(clamped)
  }

  const handlePointerDown = (event: React.PointerEvent<HTMLInputElement>) => {
    if (disabled || editing || event.button !== 0) return
    event.preventDefault()

    const startX = event.clientX
    const startValue = value
    let moved = false
    let latestValue = startValue
    lastDispatchedLiveRef.current = startValue

    const el = event.currentTarget
    if (typeof el.setPointerCapture === 'function') {
      try {
        el.setPointerCapture(event.pointerId)
      } catch {
        // Synthetic pointer events in tests may not have an active pointerId.
      }
    }

    const onPointerMove = (moveEvent: PointerEvent) => {
      const dx = moveEvent.clientX - startX
      if (!moved && Math.abs(dx) < 3) return
      if (!moved) {
        moved = true
        setDragging(true)
        document.body.style.cursor = 'ew-resize'
      }
      const modifier = moveEvent.shiftKey ? 0.1 : moveEvent.ctrlKey || moveEvent.altKey ? 5 : 1
      const delta = (dx / pixelsPerStep) * effectiveSpeed * modifier
      const next = clampAndRound(startValue + delta, min, max, decimals)
      if (next !== latestValue) {
        latestValue = next
        setPreviewValue(next)
        emitThrottledLive(next)
      }
    }

    const cleanup = () => {
      window.removeEventListener('pointermove', onPointerMove)
      window.removeEventListener('pointerup', onPointerUp)
      window.removeEventListener('pointercancel', onPointerUp)
      document.body.style.cursor = ''
      clearThrottle()
    }

    const onPointerUp = () => {
      cleanup()
      setDragging(false)
      setPreviewValue(null)
      if (moved) {
        setDraftText(latestValue.toFixed(decimals))
        if (latestValue !== startValue || onChangeRef.current) {
          onCommitRef.current(latestValue)
        }
      } else {
        editingRef.current = true
        setEditing(true)
        setDraftText(value.toFixed(decimals))
        requestAnimationFrame(() => {
          inputRef.current?.focus()
          inputRef.current?.select()
        })
      }
    }

    window.addEventListener('pointermove', onPointerMove)
    window.addEventListener('pointerup', onPointerUp)
    window.addEventListener('pointercancel', onPointerUp)
  }

  const displayedText =
    editing ? draftText : `${(previewValue ?? value).toFixed(decimals)}${unit ? ` ${unit}` : ''}`

  return (
    <label className={cn('inline-flex items-center gap-1.5 text-sm', className)}>
      {label && <span className="shrink-0 text-xs text-muted-foreground">{label}</span>}
      <input
        ref={inputRef}
        type="text"
        inputMode="decimal"
        readOnly={!editing}
        disabled={disabled}
        aria-label={ariaLabel}
        value={displayedText}
        onPointerDown={handlePointerDown}
        onFocus={() => {
          if (!dragging && !editing) {
            setDraftText(value.toFixed(decimals))
          }
        }}
        onChange={(event) => {
          if (editing) setDraftText(event.target.value)
        }}
        onBlur={() => {
          if (editingRef.current) commitText()
        }}
        onKeyDown={(event) => {
          if (editingRef.current) {
            if (event.key === 'Enter') {
              event.preventDefault()
              commitText()
              inputRef.current?.blur()
            } else if (event.key === 'Escape') {
              event.preventDefault()
              editingRef.current = false
              setEditing(false)
              setDraftText(value.toFixed(decimals))
              inputRef.current?.blur()
            }
            return
          }
          if (event.key === 'Enter' || event.key === 'F2') {
            event.preventDefault()
            editingRef.current = true
            setEditing(true)
            setDraftText(value.toFixed(decimals))
            requestAnimationFrame(() => inputRef.current?.select())
          } else if (event.key === 'ArrowUp' || event.key === 'ArrowRight') {
            event.preventDefault()
            const mult = event.shiftKey ? 0.1 : event.ctrlKey || event.altKey ? 5 : 1
            const next = clampAndRound(value + step * mult, min, max, decimals)
            if (next !== value) onCommitRef.current(next)
          } else if (event.key === 'ArrowDown' || event.key === 'ArrowLeft') {
            event.preventDefault()
            const mult = event.shiftKey ? 0.1 : event.ctrlKey || event.altKey ? 5 : 1
            const next = clampAndRound(value - step * mult, min, max, decimals)
            if (next !== value) onCommitRef.current(next)
          }
        }}
        className={cn(
          'flex h-8 w-24 rounded-md border border-input bg-background/70 px-2 text-right text-xs tabular-nums transition-colors select-none focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring disabled:cursor-not-allowed disabled:opacity-50',
          editing ? 'cursor-text select-text bg-card' : 'cursor-ew-resize hover:border-foreground/30 hover:bg-accent/35',
          dragging && 'border-primary bg-primary/10 text-primary',
          inputClassName,
        )}
      />
    </label>
  )
}

export function Vector3DragFloat({
  value,
  min,
  max,
  step,
  dragSpeed,
  decimals,
  disabled,
  labelPrefix,
  onChange,
  onCommit,
  heading,
}: {
  value: { x: number; y: number; z: number }
  min: number
  max: number
  step?: number
  dragSpeed?: number
  decimals?: number
  disabled?: boolean
  labelPrefix: string
  onChange?: (next: { x: number; y: number; z: number }) => void
  onCommit: (next: { x: number; y: number; z: number }) => void
  heading?: { value: number; onCommit: (next: number) => void; onChange?: (next: number) => void }
}) {
  return (
    <div className="flex flex-wrap items-center gap-3">
      {(['x', 'y', 'z'] as const).map((axis) => (
        <DragFloat
          key={axis}
          label={`${axis.toUpperCase()}:`}
          ariaLabel={`${labelPrefix} ${axis.toUpperCase()}`}
          value={value[axis]}
          min={min}
          max={max}
          step={step}
          dragSpeed={dragSpeed}
          decimals={decimals}
          disabled={disabled}
          inputClassName={heading ? 'w-16' : undefined}
          onChange={onChange ? (next) => onChange({ ...value, [axis]: next }) : undefined}
          onCommit={(next) => onCommit({ ...value, [axis]: next })}
        />
      ))}
      {heading && (
        <DragFloat
          label="朝向:"
          ariaLabel={`${labelPrefix} 朝向`}
          value={heading.value}
          min={-180}
          max={180}
          step={1}
          decimals={1}
          unit="°"
          disabled={disabled}
          inputClassName="w-16"
          onChange={heading.onChange}
          onCommit={heading.onCommit}
        />
      )}
    </div>
  )
}
