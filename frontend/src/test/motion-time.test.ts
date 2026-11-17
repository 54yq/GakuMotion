import { describe, expect, it } from 'vitest'
import {
  durationMismatch,
  formatMmSs,
  formatTransportTime,
  framePosition,
  lastFrameTime,
  parseTransportFrame,
  parseTransportTime,
  steppedFrame,
  validTimeline,
} from '@/motion/time'

describe('transport time formatting', () => {
  it('formats mm:ss.mmm with rounding', () => {
    expect(formatTransportTime(0)).toBe('00:00.000')
    expect(formatTransportTime(84.5)).toBe('01:24.500')
    expect(formatTransportTime(61.2345)).toBe('01:01.235')
    expect(formatTransportTime(-3)).toBe('00:00.000')
    expect(formatTransportTime(Number.NaN)).toBe('00:00.000')
  })

  it('formats library durations as mm:ss', () => {
    expect(formatMmSs(212.9)).toBe('03:32')
    expect(formatMmSs(0)).toBe('00:00')
  })
})

describe('parseTransportTime', () => {
  it('accepts decimal seconds and mm:ss.mmm', () => {
    expect(parseTransportTime('12.5')).toBeCloseTo(12.5)
    expect(parseTransportTime('01:24.500')).toBeCloseTo(84.5)
    expect(parseTransportTime(' 2:00 ')).toBeCloseTo(120)
    expect(parseTransportTime('0')).toBe(0)
  })

  it('rejects signs, exponents, partial parses and overflow', () => {
    expect(parseTransportTime('')).toBeNull()
    expect(parseTransportTime('-1')).toBeNull()
    expect(parseTransportTime('+1')).toBeNull()
    expect(parseTransportTime('1e3')).toBeNull()
    expect(parseTransportTime('NaN')).toBeNull()
    expect(parseTransportTime('12abc')).toBeNull()
    expect(parseTransportTime('1:60')).toBeNull()
    expect(parseTransportTime('1:2:3')).toBeNull()
    expect(parseTransportTime('1.2.3')).toBeNull()
    expect(parseTransportTime('9'.repeat(40))).toBeNull()
  })
})

describe('parseTransportFrame', () => {
  it('accepts 0-based non-negative integers', () => {
    expect(parseTransportFrame('0')).toBe(0)
    expect(parseTransportFrame(' 120 ')).toBe(120)
  })

  it('rejects invalid frame text', () => {
    expect(parseTransportFrame('')).toBeNull()
    expect(parseTransportFrame('-1')).toBeNull()
    expect(parseTransportFrame('1.5')).toBeNull()
    expect(parseTransportFrame('abc')).toBeNull()
    expect(parseTransportFrame('9'.repeat(20))).toBeNull()
  })
})

describe('frame math', () => {
  it('validates timelines', () => {
    expect(validTimeline(0, 60)).toBe(false)
    expect(validTimeline(100, 0)).toBe(false)
    expect(validTimeline(100, 60)).toBe(true)
  })

  it('snaps float noise near samples but keeps between-frame times', () => {
    expect(framePosition(1 / 60, 60, 1000)).toBe(1)
    expect(framePosition(0.5 / 60, 60, 1000)).toBeCloseTo(0.5)
    expect(framePosition(-1, 60, 1000)).toBe(0)
    expect(framePosition(1e9, 60, 1000)).toBe(999)
  })

  it('steps frames with terminal-hold semantics', () => {
    expect(steppedFrame(0, 60, 100, 1)).toBe(1)
    expect(steppedFrame(1 / 60, 60, 100, -1)).toBe(0)
    expect(steppedFrame(0, 60, 100, -1)).toBe(0)
    // After the last sample (terminal hold), one backward step reaches it.
    expect(steppedFrame(99 / 60 + 0.5, 60, 100, -1)).toBe(99)
    expect(steppedFrame(99 / 60, 60, 100, 1)).toBe(99)
  })

  it('detects header duration mismatch against count/fps', () => {
    expect(durationMismatch(600, 60, 10)).toBe(false)
    expect(durationMismatch(600, 60, 9.9)).toBe(true)
    expect(durationMismatch(0, 60, 0)).toBe(true)
    expect(lastFrameTime(600, 60)).toBeCloseTo(599 / 60)
  })
})
