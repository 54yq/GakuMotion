import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { TooltipProvider } from '@/components/ui/tooltip'
import { TransportBar } from '@/motion/components/TransportBar'
import { setMotionBridge } from '@/motion/bridge'
import { MotionMockBridgeClient } from '@/motion/mock'
import { motionStore } from '@/motion/store'

let bridge: MotionMockBridgeClient

function renderBar() {
  return render(
    <TooltipProvider>
      <TransportBar />
    </TooltipProvider>,
  )
}

beforeEach(async () => {
  bridge = new MotionMockBridgeClient()
  setMotionBridge(bridge)
  motionStore.reset()
  await motionStore.start()
})

afterEach(() => {
  bridge.close()
  motionStore.reset()
  setMotionBridge(null)
  vi.restoreAllMocks()
})

describe('TransportBar', () => {
  it('shows the stopped state and disables playback without a clip', () => {
    renderBar()
    expect(screen.getByTestId('transport-title')).toHaveTextContent('[停止] 未加载曲目')
    expect(screen.getByRole('button', { name: '播放' })).toBeDisabled()
    expect(screen.getByRole('slider', { name: '播放进度' })).toHaveAttribute('data-disabled', '')
  })

  it('plays and pauses a loaded motion', async () => {
    await motionStore.selectMotion('hikari-no-saki', false)
    const user = userEvent.setup()
    renderBar()
    expect(screen.getByTestId('transport-title')).toHaveTextContent('[预览] 輝きの先へ')
    await user.click(screen.getByRole('button', { name: '播放' }))
    await waitFor(() => expect(screen.getByTestId('transport-title')).toHaveTextContent('[播放]'))
    await user.click(screen.getByRole('button', { name: '暂停' }))
    await waitFor(() => expect(screen.getByTestId('transport-title')).toHaveTextContent('[暂停]'))
  })

  it('runs the three-stage scrub from the slider keyboard interaction', async () => {
    await motionStore.selectMotion('hikari-no-saki', false)
    const spy = vi.spyOn(bridge, 'request')
    renderBar()
    const slider = screen.getByRole('slider', { name: '播放进度' })
    slider.focus()
    const user = userEvent.setup()
    await user.keyboard('{ArrowRight}')
    await waitFor(() => expect(motionStore.getState().scrub.active).toBe(false))
    const methods = spy.mock.calls.map((call) => call[0])
    expect(methods).toContain('player.scrubBegin')
    expect(methods).toContain('player.scrubEnd')
    expect(methods.indexOf('player.scrubBegin')).toBeLessThan(methods.indexOf('player.scrubEnd'))
  })

  it('reports invalid time input and out-of-range frames', async () => {
    await motionStore.selectMotion('hikari-no-saki', false)
    const user = userEvent.setup()
    renderBar()
    const timeInput = screen.getByRole('textbox', { name: '时间定位' })
    await user.clear(timeInput)
    await user.type(timeInput, 'abc{Enter}')
    expect(screen.getByTestId('transport-navigation-error')).toHaveTextContent('时间格式无效')
    const frameInput = screen.getByRole('textbox', { name: '动作帧定位' })
    await user.clear(frameInput)
    await user.type(frameInput, '999999{Enter}')
    expect(screen.getByTestId('transport-navigation-error')).toHaveTextContent('帧号超出范围')
    await user.clear(frameInput)
    await user.type(frameInput, '120{Enter}')
    await waitFor(() => expect(screen.queryByTestId('transport-navigation-error')).not.toBeInTheDocument())
  })

  it('disables prev/next while ensemble is active', async () => {
    await motionStore.selectMotion('hikari-no-saki', false)
    renderBar()
    expect(screen.getByRole('button', { name: '上一曲' })).toBeEnabled()
    bridge.push('ensemble.changed', { ...motionStore.getState().ensemble, active: true })
    await waitFor(() => expect(screen.getByRole('button', { name: '上一曲' })).toBeDisabled())
    expect(screen.getByRole('button', { name: '下一曲' })).toBeDisabled()
  })

  it('saves volume on slider commit', async () => {
    const spy = vi.spyOn(bridge, 'request')
    renderBar()
    const slider = screen.getByRole('slider', { name: '音乐音量' })
    slider.focus()
    const user = userEvent.setup()
    await user.keyboard('{ArrowLeft}')
    const methods = spy.mock.calls.map((call) => call[0])
    expect(methods).toContain('player.setVolume')
    expect(methods).toContain('player.saveVolume')
  })
})
