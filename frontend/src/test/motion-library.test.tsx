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
  it('lists entries and filters locally by search and track tags', async () => {
    const user = userEvent.setup()
    render(<LibraryPage />)
    expect(screen.getByTestId('library-count')).toHaveTextContent('显示 5 / 5 个曲目')

    // Track filter dropdown menu (funnel trigger + checkbox items)
    await user.click(screen.getByRole('button', { name: '轨道筛选' }))
    await user.click(await screen.findByRole('menuitemcheckbox', { name: '含镜头' }))
    expect(screen.getByTestId('library-count')).toHaveTextContent('显示 3 / 5 个曲目')
    await user.click(screen.getByRole('button', { name: '轨道筛选' }))
    await user.click(await screen.findByRole('menuitemcheckbox', { name: '全部曲目' }))
    expect(screen.getByTestId('library-count')).toHaveTextContent('显示 5 / 5 个曲目')

    await user.type(screen.getByRole('textbox', { name: '搜索曲目' }), 'Starlight')
    expect(screen.getByTestId('library-count')).toHaveTextContent('显示 1 / 5 个曲目')
    expect(screen.getByTestId('library-row-starlight-step')).toBeInTheDocument()
    expect(screen.queryByTestId('library-row-hikari-no-saki')).not.toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: '清空' }))
    expect(screen.getByTestId('library-count')).toHaveTextContent('显示 5 / 5 个曲目')
  })

  it('supports implicit list sorting, row hover quick actions, and context menu', async () => {
    const user = userEvent.setup()
    render(<LibraryPage />)
    expect(screen.getByTestId('library-table-scroll')).toBeInTheDocument()

    // Click column header to sort by title
    await user.click(screen.getByRole('button', { name: '演出曲目' }))
    expect(motionStore.getState().librarySort).toBe('title')

    // Hover status play button starts playback, then swaps to pause button; blank area clears selection
    await user.click(screen.getByRole('button', { name: '播放 Starlight Step' }))
    await waitFor(() => expect(motionStore.getState().loaded.motionKey).toBe('starlight-step'))
    expect(motionStore.getState().player.state).toBe('playing')

    await user.click(screen.getByRole('button', { name: '暂停 Starlight Step' }))
    await waitFor(() => expect(motionStore.getState().player.state).toBe('paused'))

    await user.click(screen.getByRole('button', { name: '播放 Starlight Step' }))
    await waitFor(() => expect(motionStore.getState().player.state).toBe('playing'))

    const starlightRow = screen.getByTestId('library-row-starlight-step')
    await user.click(starlightRow)
    expect(starlightRow).toHaveAttribute('aria-selected', 'true')
    await user.click(screen.getByTestId('library-table-scroll'))
    expect(starlightRow).toHaveAttribute('aria-selected', 'false')

    // Right-click context menu on a row opens single-track actions
    const row = screen.getByTestId('library-row-encore-demo')
    await user.pointer({ keys: '[MouseRight]', target: row })
    const menu = await screen.findByTestId('library-context-menu')
    await user.click(within(menu).getByRole('menuitem', { name: '从曲库删除…' }))

    const dialog = await screen.findByTestId('delete-dialog')
    await user.click(within(dialog).getByRole('button', { name: '永久删除' }))
    await waitFor(() => expect(screen.queryByTestId('library-row-encore-demo')).not.toBeInTheDocument())
  })

  it('selects on click and plays on double click', async () => {
    const user = userEvent.setup()
    const spy = vi.spyOn(bridge, 'request')
    render(<LibraryPage />)
    const row = screen.getByTestId('library-row-hikari-no-saki')
    await user.click(row)
    expect(motionStore.getState().selectedLibraryKey).toBe('hikari-no-saki')
    expect(row).toHaveAttribute('aria-selected', 'true')
    await user.dblClick(row)
    await waitFor(() => expect(motionStore.getState().player.state).toBe('playing'))
    const selectCalls = spy.mock.calls.filter((call) => call[0] === 'library.select')
    expect(selectCalls[selectCalls.length - 1][1]).toMatchObject({ key: 'hikari-no-saki', autoplay: true })
  })

  it('confirms permanent deletion with the revision guard', async () => {
    const user = userEvent.setup()
    render(<LibraryPage />)
    await user.click(screen.getByRole('button', { name: '删除 Encore（演示）' }))
    const dialog = screen.getByTestId('delete-dialog')
    expect(within(dialog).getByText(/永久删除整个曲目目录/)).toBeInTheDocument()
    await user.click(within(dialog).getByRole('button', { name: '永久删除' }))
    await waitFor(() => expect(screen.queryByTestId('library-row-encore-demo')).not.toBeInTheDocument())
  })

  it('blocks deletion when the library changed after the dialog opened', async () => {
    const user = userEvent.setup()
    render(<LibraryPage />)
    await user.click(screen.getByRole('button', { name: '删除 Encore（演示）' }))
    const dialog = screen.getByTestId('delete-dialog')
    // A concurrent library change bumps the revision.
    await bridge.request('library.delete', { key: 'practice-room', revision: motionStore.getState().library.revision })
    await waitFor(() => expect(within(dialog).getByText('曲库已变化，请取消后重新选择曲目。')).toBeInTheDocument())
    expect(within(dialog).getByRole('button', { name: '永久删除' })).toBeDisabled()
  })

  it('renders the real import dialog and blocks import when phase1 pipeline is unsupported', async () => {
    bridge.close()
    bridge = new MotionMockBridgeClient({ capabilityProfile: 'phase1' })
    setMotionBridge(bridge)
    motionStore.reset()
    await motionStore.start()

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

  it('imports a performance through full mock dialog & progress pipeline', async () => {
    const user = userEvent.setup()
    render(<LibraryPage />)
    await user.click(screen.getByRole('button', { name: '添加到曲库' }))
    const dialog = await screen.findByTestId('import-dialog')
    await user.type(within(dialog).getByLabelText('曲目标题'), 'StageLive')
    await user.type(within(dialog).getByLabelText('副标题 / 作者'), '作者A, 作者B')
    await user.click(within(dialog).getAllByRole('button', { name: '浏览…' })[0])
    await waitFor(() => expect(within(dialog).getByRole('button', { name: '导入' })).toBeEnabled())
    await user.click(within(dialog).getByRole('button', { name: '导入' }))
    await waitFor(() => expect(screen.getByTestId('library-count')).toHaveTextContent('显示 6 / 6 个曲目'))
    expect(screen.getByText(/作者A \/ 作者B/)).toBeInTheDocument()
  })

  it('renders track cover thumbnails in library rows and probes mp3 cover/artist on import', async () => {
    const user = userEvent.setup()
    render(<LibraryPage />)
    await waitFor(() => expect(screen.getByTestId('library-cover-hikari-no-saki')).toBeInTheDocument())

    await user.click(screen.getByRole('button', { name: '添加到曲库' }))
    const dialog = await screen.findByTestId('import-dialog')
    await motionStore.importPickSlotFile('audio', 'C:\\music\\stage_live.mp3')
    await waitFor(() => expect(within(dialog).getByTestId('import-audio-preview')).toBeInTheDocument())
    expect(within(dialog).getByTestId('import-cover-preview')).toBeInTheDocument()
    expect(within(dialog).getByLabelText('副标题 / 作者')).toHaveValue('初星学園')
  })
})
