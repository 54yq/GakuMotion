import { render, screen, waitFor, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { LibraryPage } from '@/motion/pages/LibraryPage'
import { setMotionBridge } from '@/motion/bridge'
import { MotionMockBridgeClient } from '@/motion/mock'
import { motionStore } from '@/motion/store'

let bridge: MotionMockBridgeClient

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

describe('LibraryPage', () => {
  it('lists entries and filters locally by search', async () => {
    const user = userEvent.setup()
    render(<LibraryPage />)
    expect(screen.getByTestId('library-count')).toHaveTextContent('显示 5 / 5 个曲目')
    await user.type(screen.getByRole('textbox', { name: '搜索曲目' }), 'Starlight')
    expect(screen.getByTestId('library-count')).toHaveTextContent('显示 1 / 5 个曲目')
    expect(screen.getByTestId('library-row-starlight-step')).toBeInTheDocument()
    expect(screen.queryByTestId('library-row-hikari-no-saki')).not.toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: '清空' }))
    expect(screen.getByTestId('library-count')).toHaveTextContent('显示 5 / 5 个曲目')
  })

  it('selects on click and plays on double click', async () => {
    const user = userEvent.setup()
    const spy = vi.spyOn(bridge, 'request')
    render(<LibraryPage />)
    const row = screen.getByTestId('library-row-hikari-no-saki')
    await user.click(row)
    expect(motionStore.getState().selectedLibraryKey).toBe('hikari-no-saki')
    expect(screen.getByText('选中：輝きの先へ')).toBeInTheDocument()
    await user.dblClick(row)
    await waitFor(() => expect(motionStore.getState().player.state).toBe('playing'))
    const selectCalls = spy.mock.calls.filter((call) => call[0] === 'library.select')
    expect(selectCalls[selectCalls.length - 1][1]).toMatchObject({ key: 'hikari-no-saki', autoplay: true })
  })

  it('confirms permanent deletion with the revision guard', async () => {
    const user = userEvent.setup()
    render(<LibraryPage />)
    await user.click(screen.getByTestId('library-row-encore-demo'))
    await user.click(screen.getByRole('button', { name: '删除选中' }))
    const dialog = screen.getByTestId('delete-dialog')
    expect(within(dialog).getByText(/永久删除整个曲目目录/)).toBeInTheDocument()
    await user.click(within(dialog).getByRole('button', { name: '永久删除' }))
    await waitFor(() => expect(screen.queryByTestId('library-row-encore-demo')).not.toBeInTheDocument())
  })

  it('blocks deletion when the library revision changes while the dialog is open', async () => {
    const user = userEvent.setup()
    render(<LibraryPage />)
    await user.click(screen.getByTestId('library-row-encore-demo'))
    await user.click(screen.getByRole('button', { name: '删除选中' }))
    const dialog = screen.getByTestId('delete-dialog')
    // A concurrent library change bumps the revision.
    await bridge.request('library.delete', { key: 'practice-room', revision: motionStore.getState().library.revision })
    await waitFor(() => expect(within(dialog).getByText('曲库已变化，请取消后重新选择曲目。')).toBeInTheDocument())
    expect(within(dialog).getByRole('button', { name: '永久删除' })).toBeDisabled()
  })

  it('renders the real import dialog and blocks import while the pipeline is unsupported', async () => {
    const user = userEvent.setup()
    render(<LibraryPage />)
    await user.click(screen.getByRole('button', { name: '添加到曲库' }))
    const dialog = await screen.findByTestId('import-dialog')
    expect(within(dialog).getByText('当前原生桥未提供导入管线（library.import），无法真正导入。')).toBeInTheDocument()
    expect(within(dialog).getByText('动作')).toBeInTheDocument()
    expect(within(dialog).getByText('表情')).toBeInTheDocument()
    expect(within(dialog).getByText('镜头')).toBeInTheDocument()
    expect(within(dialog).getByText('音频')).toBeInTheDocument()
    expect(within(dialog).getByRole('button', { name: '导入' })).toBeDisabled()
    expect(within(dialog).getAllByRole('button', { name: '浏览…' })[0]).toBeDisabled()
  })
})
