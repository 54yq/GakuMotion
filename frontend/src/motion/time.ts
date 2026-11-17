// Pure ports of GkTransportTime.hpp (GakuMotion dev). Keep numeric behavior
// identical: float-storage noise snapping, 0-based frames, locale-independent
// parsing. Display formatting never mutates the continuously stored time.

const FLOAT_EPSILON = 1.1920928955078125e-7
const FLOAT_MAX = 3.4028234663852886e38

export function validTimeline(totalFrames: number, fps: number): boolean {
  return totalFrames > 0 && Number.isFinite(fps) && fps > 0 && (totalFrames - 1) / fps <= FLOAT_MAX
}

export function lastFrameTime(totalFrames: number, fps: number): number {
  return validTimeline(totalFrames, fps) ? (totalFrames - 1) / fps : 0
}

export function durationMismatch(totalFrames: number, fps: number, duration: number): boolean {
  if (!validTimeline(totalFrames, fps) || !Number.isFinite(duration) || duration < 0) return true
  const expected = totalFrames / fps
  const tolerance = Math.max(1e-4, expected * FLOAT_EPSILON * 2)
  return Math.abs(expected - duration) > tolerance
}

export function framePosition(seconds: number, fps: number, totalFrames: number): number {
  if (!validTimeline(totalFrames, fps) || !Number.isFinite(seconds)) return 0
  let position = Math.min(Math.max(seconds * fps, 0), totalFrames - 1)
  const nearest = Math.round(position)
  const tolerance = Math.min(0.125, Math.max(1e-6, Math.abs(position) * FLOAT_EPSILON * 2))
  if (Math.abs(position - nearest) <= tolerance) position = nearest
  return position
}

export function steppedFrame(seconds: number, fps: number, totalFrames: number, delta: number): number {
  let position = framePosition(seconds, fps, totalFrames)
  if (validTimeline(totalFrames, fps) && Number.isFinite(seconds)) {
    const raw = seconds * fps
    const tolerance = Math.min(0.125, Math.max(1e-6, Math.abs(raw) * FLOAT_EPSILON * 2))
    if (raw > totalFrames - 1 + tolerance) position = raw
  }
  const base = delta > 0 ? Math.floor(position) : Math.ceil(position)
  return Math.min(Math.max(base + delta, 0), totalFrames > 0 ? totalFrames - 1 : 0)
}

export function formatTransportTime(seconds: number): string {
  if (!Number.isFinite(seconds) || seconds < 0) seconds = 0
  if (seconds > 1e12) return seconds.toFixed(3)
  const milliseconds = Math.round(seconds * 1000)
  const mm = Math.floor(milliseconds / 60000)
  const ss = Math.floor(milliseconds / 1000) % 60
  const mmm = milliseconds % 1000
  return `${String(mm).padStart(2, '0')}:${String(ss).padStart(2, '0')}.${String(mmm).padStart(3, '0')}`
}

export function formatMmSs(seconds: number): string {
  if (!Number.isFinite(seconds) || seconds < 0) seconds = 0
  const totalSec = Math.floor(seconds)
  const mm = Math.floor(totalSec / 60)
  const ss = totalSec % 60
  return `${String(mm).padStart(2, '0')}:${String(ss).padStart(2, '0')}`
}

function parseDecimal(part: string, allowFraction: boolean): number | null {
  if (part.length === 0) return null
  let value = 0
  let dot = false
  let digit = false
  let scale = 0.1
  for (const char of part) {
    if (char === '.' && allowFraction && !dot) {
      dot = true
      continue
    }
    if (char < '0' || char > '9') return null
    digit = true
    if (dot) {
      value += (char.charCodeAt(0) - 48) * scale
      scale *= 0.1
    } else {
      value = value * 10 + (char.charCodeAt(0) - 48)
    }
    if (!Number.isFinite(value) || value > FLOAT_MAX) return null
  }
  return digit ? value : null
}

// Decimal seconds or minutes:seconds. Rejects signs, exponents, NaN/Inf,
// partial parses and overflowing values, exactly like ParseTransportTime.
export function parseTransportTime(input: string): number | null {
  const trimmed = input.trim()
  if (trimmed.length === 0) return null
  const colon = trimmed.indexOf(':')
  let seconds: number | null
  if (colon === -1) {
    seconds = parseDecimal(trimmed, true)
  } else {
    const minutes = parseDecimal(trimmed.slice(0, colon), false)
    const remainder = parseDecimal(trimmed.slice(colon + 1), true)
    if (minutes === null || remainder === null || remainder >= 60) return null
    seconds = minutes * 60 + remainder
  }
  if (seconds === null || !Number.isFinite(seconds) || seconds > FLOAT_MAX) return null
  return seconds
}

// Non-negative integer, 0-based. Rejects signs, decimals and overflow.
export function parseTransportFrame(input: string): number | null {
  const trimmed = input.trim()
  if (trimmed.length === 0) return null
  let value = 0
  for (const char of trimmed) {
    if (char < '0' || char > '9') return null
    value = value * 10 + (char.charCodeAt(0) - 48)
    if (value > Number.MAX_SAFE_INTEGER) return null
  }
  return value
}
