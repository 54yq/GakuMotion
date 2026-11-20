import { fireEvent, render, screen, waitFor, within } from '@testing-library/react'
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

  it('accepts dropped .mp3/.flac/.opus audio files in openImportNew and renders default covers', async () => {
    render(<LibraryPage />)
    // Track without custom cover renders DefaultAlbumCover
    expect(screen.getByTestId('library-default-cover-encore-demo')).toBeInTheDocument()
    // Eager preload populates coverCache for tracks with covers even before playback
    await waitFor(() => expect(motionStore.getState().coverCache['hikari-no-saki']).toBeTruthy())

    motionStore.handleDropFiles(['C:\\Drop\\Gimme_motion.vmd', 'C:\\Drop\\Gimme_song.mp3'])
    const dialog = await screen.findByTestId('import-dialog')
    await waitFor(() => expect(within(dialog).getByRole('button', { name: '导入' })).toBeEnabled())
    expect(motionStore.importFormPicks().audio).toBe('C:\\Drop\\Gimme_song.mp3')
    await waitFor(() => expect(within(dialog).getByTestId('import-audio-preview')).toBeInTheDocument())
  })

  it('auto-selects newly imported entry by exact id even with duplicate titles and supports 1:1 cover editing with ScreenshotManager', async () => {
    const user = userEvent.setup()
    render(<LibraryPage />)
    const spy = vi.spyOn(bridge, 'request')

    // Import a new track with the same title as an existing track ("Starlight Step")
    await motionStore.openImportNew(['C:\\Drop\\Starlight_motion.vmd', 'C:\\Drop\\Starlight.flac'])
    const dialog = await screen.findByTestId('import-dialog')
    await user.clear(within(dialog).getByLabelText('曲目标题'))
    await user.type(within(dialog).getByLabelText('曲目标题'), 'Starlight Step')
    await user.click(within(dialog).getByTestId('import-edit-cover-btn'))

    const editor = await screen.findByTestId('cover-editor-modal')
    await user.click(within(editor).getByTestId('cover-capture-screenshot-btn'))
    await waitFor(() => expect(spy.mock.calls.some((c) => c[0] === 'screenshot.capture')).toBe(true))
    await user.click(within(editor).getByTestId('cover-save-btn'))

    await user.click(within(dialog).getByRole('button', { name: '导入' }))
    await waitFor(() => expect(screen.getByTestId('library-count')).toHaveTextContent('显示 6 / 6 个曲目'))

    // Verify newly imported track is selected and loaded with its exact id (not old 'starlight-step')
    const state = motionStore.getState()
    expect(state.selectedLibraryKey).toBeTruthy()
    expect(state.selectedLibraryKey).not.toBe('starlight-step')
    expect(state.loaded.motionKey).toBe(state.selectedLibraryKey)
    expect(state.coverCache[state.selectedLibraryKey!]).toBeTruthy()
  })

  it('wraps long paths without overflow in delete confirmation and retains covers across library refresh', async () => {
    const user = userEvent.setup()
    render(<LibraryPage />)
    await waitFor(() => expect(screen.getByTestId('library-cover-hikari-no-saki')).toBeInTheDocument())

    // Refresh library busts & re-fetches mounted covers cleanly
    await motionStore.refreshLibrary()
    await waitFor(() => expect(motionStore.getState().library.refreshing).toBe(false))
    await waitFor(() => expect(screen.getByTestId('library-cover-hikari-no-saki')).toBeInTheDocument())

    await user.click(screen.getByRole('button', { name: '删除 Encore（演示）' }))
    const deleteDialog = await screen.findByTestId('delete-dialog')
    expect(deleteDialog.className).toContain('max-h-[85vh]')
    expect(within(deleteDialog).getByText(/gakumas-local\/motions\/encore-demo/).className).toContain('break-all')
  })

  it('accepts native host drop.files inside CoverEditorModal across library, import, and non-library transport contexts', async () => {
    const spy = vi.spyOn(bridge, 'request')
    render(<LibraryPage />)

    // 1) Open CoverEditorModal from library track cover
    motionStore.openCoverEditorForTrack('encore-demo', 'library')
    const modal = await screen.findByTestId('cover-editor-modal')
    expect(modal).toBeInTheDocument()
    expect(motionStore.canAcceptDrop()).toBe(true)

    await waitFor(() =>
      expect(spy.mock.calls.some((c) => c[0] === 'drop.setTarget' && (c[1] as { enabled: boolean }).enabled === true)).toBe(true),
    )

    // Native drop delivers an image path via drop.poll -> loadCoverPreviewFromPath
    bridge.simulateFileDrop(['C:\\Images\\cover_art.png'])
    await waitFor(() =>
      expect(spy.mock.calls.some((c) => c[0] === 'library.readImagePreview' && (c[1] as { path: string }).path === 'C:\\Images\\cover_art.png')).toBe(true),
    )
    await waitFor(() => expect(motionStore.getState().coverEditor.sourceImageUri).toMatch(/^data:image\//))
    expect(motionStore.getState().importForm.open).toBe(false)

    // Dropping non-image shows an error inside CoverEditorModal without opening ImportDialog
    motionStore.handleDropFiles(['C:\\Drop\\not_an_image.vmd'])
    expect(motionStore.getState().coverEditor.error).toContain('仅支持拖入图片文件')
    expect(motionStore.getState().importForm.open).toBe(false)

    // 2) Open CoverEditorModal from ImportDialog (importForm.open=true must not block cover drop)
    motionStore.closeCoverEditor()
    await motionStore.openImportNew(['C:\\Drop\\Dance_motion.vmd'])
    motionStore.openCoverEditorForImport()
    expect(motionStore.canAcceptDrop()).toBe(true)
    motionStore.handleDropFiles(['C:\\Images\\import_cover.webp'])
    await waitFor(() =>
      expect(spy.mock.calls.some((c) => c[0] === 'library.readImagePreview' && (c[1] as { path: string }).path === 'C:\\Images\\import_cover.webp')).toBe(true),
    )

    // 3) Open CoverEditorModal from TransportBar while on a non-library page
    motionStore.closeCoverEditor()
    motionStore.closeImportForm()
    motionStore.setPage('motion')
    motionStore.openCoverEditorForTrack('encore-demo', 'transport')
    expect(motionStore.canAcceptDrop()).toBe(true)
    motionStore.handleDropFiles(['C:\\Images\\transport_cover.jpg'])
    await waitFor(() =>
      expect(spy.mock.calls.some((c) => c[0] === 'library.readImagePreview' && (c[1] as { path: string }).path === 'C:\\Images\\transport_cover.jpg')).toBe(true),
    )
  })

  it('accepts HTML5 drag-and-drop image files on CoverEditorModal dialog and cropper without wiping on invalid drop', async () => {
    render(<LibraryPage />)
    motionStore.openCoverEditorForTrack('encore-demo', 'library')
    const modal = await screen.findByTestId('cover-editor-modal')
    const cropper = within(modal).getByTestId('cover-crop-canvas')

    const validImage = new File([new Uint8Array([137, 80, 78, 71])], 'custom.png', { type: 'image/png' })
    fireEvent.dragOver(modal, { dataTransfer: { files: [validImage], dropEffect: '' } })
    fireEvent.drop(modal, { dataTransfer: { files: [validImage] } })

    await waitFor(() => expect(motionStore.getState().coverEditor.sourceImageUri).toMatch(/^data:image\/png/))
    const loadedUri = motionStore.getState().coverEditor.sourceImageUri

    // Invalid file dropped on cropper shows error and preserves loaded preview
    const invalidFile = new File(['hello'], 'notes.txt', { type: 'text/plain' })
    fireEvent.drop(cropper, { dataTransfer: { files: [invalidFile] } })
    expect(motionStore.getState().coverEditor.error).toContain('仅支持拖入图片文件')
    expect(motionStore.getState().coverEditor.sourceImageUri).toBe(loadedUri)
  })
})
