import { fireEvent, render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { DragFloat } from '@/motion/components/DragFloat'

afterEach(() => {
  vi.useRealTimers()
  vi.restoreAllMocks()
})

describe('DragFloat', () => {
  it('drags beyond bounds with throttled live preview and commits final clamped value on release', () => {
    vi.useFakeTimers()
    const onChange = vi.fn()
    const onCommit = vi.fn()

    render(
      <DragFloat
        ariaLabel="测试拖动数值"
        value={1}
        min={-5}
        max={5}
        step={0.1}
        dragSpeed={0.1}
        pixelsPerStep={2}
        throttleMs={50}
        onChange={onChange}
        onCommit={onCommit}
      />,
    )

    const input = screen.getByRole('textbox', { name: '测试拖动数值' })
    fireEvent.pointerDown(input, { button: 0, clientX: 100 })
    // Move horizontally well outside the control bounds -> +40px = +2.00 units
    fireEvent.pointerMove(window, { clientX: 140 })
    expect(onChange).toHaveBeenCalledTimes(1)
    expect(onChange).toHaveBeenLastCalledWith(3)

    // Subsequent rapid move within throttleMs is rate-limited
    fireEvent.pointerMove(window, { clientX: 144 })
    expect(onChange).toHaveBeenCalledTimes(1)

    vi.advanceTimersByTime(60)
    expect(onChange).toHaveBeenLastCalledWith(3.2)

    // Release commits final clamped value and cleans up listeners
    fireEvent.pointerMove(window, { clientX: 400 })
    fireEvent.pointerUp(window)
    expect(onCommit).toHaveBeenCalledTimes(1)
    expect(onCommit).toHaveBeenCalledWith(5)
  })

  it('enters text editing on click, commits Enter, and cancels Escape', async () => {
    const user = userEvent.setup()
    const onCommit = vi.fn()

    render(
      <DragFloat
        ariaLabel="点击编辑数值"
        value={2}
        min={0}
        max={10}
        step={0.1}
        decimals={2}
        onCommit={onCommit}
      />,
    )

    const input = screen.getByRole('textbox', { name: '点击编辑数值' })
    await user.click(input)
    await user.clear(input)
    await user.type(input, '4.5{Enter}')
    expect(onCommit).toHaveBeenCalledWith(4.5)

    // Escape restores previous value without committing
    onCommit.mockClear()
    await user.click(input)
    await user.clear(input)
    await user.type(input, '9{Escape}')
    expect(onCommit).not.toHaveBeenCalled()
  })
})
