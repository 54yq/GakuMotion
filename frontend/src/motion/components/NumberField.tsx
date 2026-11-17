import * as React from 'react'
import { Input } from '@/components/ui/input'
import { cn } from '@/lib/utils'

interface NumberFieldProps {
  value: number
  min: number
  max: number
  step?: number
  decimals?: number
  disabled?: boolean
  label?: string
  ariaLabel: string
  className?: string
  onCommit: (value: number) => void
}

// ImGui DragFloat equivalent: edits are local drafts; Enter/blur commits the
// clamped value, Escape restores the authoritative value.
export function NumberField({
  value,
  min,
  max,
  step = 0.01,
  decimals = 2,
  disabled,
  label,
  ariaLabel,
  className,
  onCommit,
}: NumberFieldProps) {
  const [text, setText] = React.useState(value.toFixed(decimals))
  const [editing, setEditing] = React.useState(false)

  React.useEffect(() => {
    if (!editing) setText(value.toFixed(decimals))
  }, [value, decimals, editing])

  const commit = () => {
    setEditing(false)
    const parsed = Number(text)
    if (!Number.isFinite(parsed)) {
      setText(value.toFixed(decimals))
      return
    }
    const clamped = Math.min(Math.max(parsed, min), max)
    setText(clamped.toFixed(decimals))
    if (clamped !== value) onCommit(clamped)
  }

  return (
    <label className={cn('flex items-center gap-1.5 text-sm', className)}>
      {label && <span className="shrink-0 text-muted-foreground">{label}</span>}
      <Input
        type="number"
        inputMode="decimal"
        className="h-8 w-24 px-2 text-right tabular-nums"
        value={text}
        min={min}
        max={max}
        step={step}
        disabled={disabled}
        aria-label={ariaLabel}
        onFocus={() => setEditing(true)}
        onChange={(event) => setText(event.target.value)}
        onBlur={commit}
        onKeyDown={(event) => {
          if (event.key === 'Enter') {
            event.preventDefault()
            commit()
          } else if (event.key === 'Escape') {
            setEditing(false)
            setText(value.toFixed(decimals))
          }
        }}
      />
    </label>
  )
}

export function Vector3Field({
  value,
  min,
  max,
  step,
  decimals,
  disabled,
  labelPrefix,
  onCommit,
}: {
  value: { x: number; y: number; z: number }
  min: number
  max: number
  step?: number
  decimals?: number
  disabled?: boolean
  labelPrefix: string
  onCommit: (next: { x: number; y: number; z: number }) => void
}) {
  return (
    <div className="flex flex-wrap items-center gap-3">
      {(['x', 'y', 'z'] as const).map((axis) => (
        <NumberField
          key={axis}
          label={`${axis.toUpperCase()}:`}
          ariaLabel={`${labelPrefix} ${axis.toUpperCase()}`}
          value={value[axis]}
          min={min}
          max={max}
          step={step}
          decimals={decimals}
          disabled={disabled}
          onCommit={(next) => onCommit({ ...value, [axis]: next })}
        />
      ))}
    </div>
  )
}
