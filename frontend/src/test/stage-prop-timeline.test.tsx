import { fireEvent, render, screen, waitFor } from '@testing-library/react'
import { describe, expect, it, vi } from 'vitest'
import { StagePropTimeline } from '@/motion/components/StagePropTimeline'
import type { StagePropCue } from '@/motion/protocol'

const zero = { x: 0, y: 0, z: 0 }
const hold: StagePropCue = {
  prop: 11, place: 1, left: false, first: 1484, last: 1690,
  position: { x: -0.42, y: -0.45, z: 0.86 }, rotation: zero, anchor: 1, movement: 0,
}

describe('stage prop timeline', () => {
  it('edits the foreground clip, previews its exit and saves the chosen movement', async () => {
    const command = vi.fn(async () => true)
    render(<StagePropTimeline cues={[hold]} time={1500 / 30} command={command} />)
    fireEvent.click(screen.getByRole('button', { name: '镜头前' }))
    expect(command).toHaveBeenCalledWith('externalStage.cueSeek', { frame: 1484 }, true)
    fireEvent.change(screen.getByLabelText('移动方式'), { target: { value: '2' } })
    expect(screen.getByText('终点位置（画面 X/Y、距离 Z）')).toBeInTheDocument()
    await waitFor(() => expect(command).toHaveBeenCalledWith('externalStage.cuePreview',
      expect.objectContaining({ index: 0, anchor: 1, movement: 2 }), true))
    fireEvent.click(screen.getByRole('button', { name: '保存片段' }))
    await waitFor(() => expect(command).toHaveBeenCalledWith('externalStage.cueSave',
      expect.objectContaining({ index: 0, first: 1484, last: 1690, movement: 2 })))
    await waitFor(() => expect(screen.queryByText('可莉饼 B · 片段属性')).not.toBeInTheDocument())
  })

  it('rejects overlapping edits and keeps a draft open after a failed save', async () => {
    const command = vi.fn(async (action: string) => action !== 'externalStage.cueSave')
    render(<StagePropTimeline cues={[hold, { ...hold, first: 1690, last: 1710, movement: 2 }]}
      time={1500 / 30} command={command} />)
    fireEvent.click(screen.getByRole('button', { name: '镜头前' }))
    const frames = screen.getAllByRole('spinbutton')
    fireEvent.change(frames[1], { target: { value: '1692' } })
    expect(screen.getByRole('alert')).toHaveTextContent('不能重叠')
    expect(screen.getByRole('button', { name: '保存片段' })).toBeDisabled()
    fireEvent.change(frames[1], { target: { value: '1690' } })
    fireEvent.click(screen.getByRole('button', { name: '保存片段' }))
    await waitFor(() => expect(command).toHaveBeenCalledWith('externalStage.cueSave', expect.anything()))
    expect(screen.getByText('可莉饼 B · 片段属性')).toBeInTheDocument()
  })
})
