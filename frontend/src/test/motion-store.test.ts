import { afterEach, describe, expect, it, vi } from 'vitest'
import { MotionMockBridgeClient } from '@/motion/mock'
import { BridgeError } from '@/motion/protocol'
import { MotionStore } from '@/motion/store'

async function startedStore() {
  const bridge = new MotionMockBridgeClient()
  const store = new MotionStore(bridge)
  await store.start()
  return { bridge, store }
}

afterEach(() => {
  vi.restoreAllMocks()
})

describe('MotionStore startup', () => {
  it('negotiates capabilities and loads the snapshot', async () => {
    const { bridge, store } = await startedStore()
    const state = store.getState()
    expect(state.ready).toBe(true)
    expect(state.connected).toBe(true)
    expect(state.bridgeKind).toBe('mock')
    expect(state.library.entries.length).toBe(5)
    expect(state.permission.injectionAllowed).toBe(true)
    expect(store.supports('player.play')).toBe(true)
    // Reserved methods stay unavailable even against the mock.
    expect(store.supports('library.import')).toBe(false)
    expect(store.supports('dialog.pickFile')).toBe(false)
    expect(store.supports('ensemble.set')).toBe(false)
    bridge.close()
  })
})

describe('MotionStore transport', () => {
  it('plays, pauses and stops a loaded motion', async () => {
    const { bridge, store } = await startedStore()
    await store.selectMotion('hikari-no-saki', true)
    expect(store.getState().player.state).toBe('playing')
    await store.pause()
    expect(store.getState().player.state).toBe('paused')
    await store.play()
    expect(store.getState().player.state).toBe('playing')
    await store.stop()
    expect(store.getState().player.state).toBe('stopped')
    expect(store.getState().player.time).toBe(0)
    bridge.close()
  })

  it('sends the three-stage scrub sequence', async () => {
    const { bridge, store } = await startedStore()
    await store.selectMotion('hikari-no-saki', false)
    const spy = vi.spyOn(bridge, 'request')
    store.scrubStart(10)
    store.scrubMove(20)
    store.scrubCommit(30)
    expect(spy.mock.calls.map((call) => call[0])).toEqual([
      'player.scrubBegin',
      'player.scrubUpdate',
      'player.scrubEnd',
    ])
    expect(store.getState().scrub.active).toBe(false)
    bridge.close()
  })

  it('drops an active scrub without committing on permission loss', async () => {
    const { bridge, store } = await startedStore()
    await store.selectMotion('hikari-no-saki', true)
    store.scrubStart(10)
    const spy = vi.spyOn(bridge, 'request')
    bridge.setPermission(false, '截图保护未就绪')
    expect(store.getState().scrub.active).toBe(false)
    expect(store.getState().permission.injectionAllowed).toBe(false)
    expect(spy.mock.calls.map((call) => call[0])).not.toContain('player.scrubEnd')
    bridge.close()
  })

  it('drops scrub when the loaded motion identity changes', async () => {
    const { bridge, store } = await startedStore()
    await store.selectMotion('hikari-no-saki', true)
    store.scrubStart(10)
    await store.selectMotion('starlight-step', true)
    expect(store.getState().scrub.active).toBe(false)
    bridge.close()
  })

  it('validates time and frame navigation inputs', async () => {
    const { bridge, store } = await startedStore()
    await store.selectMotion('hikari-no-saki', false)
    await store.seekTime('abc')
    expect(store.getState().navigationError).toContain('时间格式无效')
    await store.seekTime('99999')
    expect(store.getState().navigationError).toContain('时间超出演出时长')
    await store.seekTime('00:10.000')
    expect(store.getState().navigationError).toBeNull()
    expect(store.getState().player.time).toBeCloseTo(10)
    await store.seekFrame('-1')
    expect(store.getState().navigationError).toContain('帧号格式无效')
    await store.seekFrame('999999')
    expect(store.getState().navigationError).toContain('帧号超出范围')
    await store.seekFrame('60')
    expect(store.getState().navigationError).toBeNull()
    expect(store.getState().player.state).toBe('paused')
    bridge.close()
  })

  it('saves volume only on commit', async () => {
    const { bridge, store } = await startedStore()
    const spy = vi.spyOn(bridge, 'request')
    store.setVolume(40)
    expect(spy.mock.calls.map((call) => call[0])).not.toContain('player.saveVolume')
    store.commitVolume(40)
    const methods = spy.mock.calls.map((call) => call[0])
    expect(methods).toContain('player.setVolume')
    expect(methods).toContain('player.saveVolume')
    await new Promise((resolve) => setTimeout(resolve, 30))
    expect(bridge.savedVolume()).toBeCloseTo(0.4)
    bridge.close()
  })
})

describe('MotionStore library guards', () => {
  it('selects with the current revision and reports stale revisions', async () => {
    const { bridge, store } = await startedStore()
    await store.selectMotion('hikari-no-saki', true)
    expect(store.getState().loaded.motionKey).toBe('hikari-no-saki')
    // Force a stale revision by deleting through the bridge directly.
    await bridge.request('library.delete', { key: 'encore-demo', revision: store.getState().library.revision })
    vi.spyOn(bridge, 'request').mockImplementationOnce(async () => {
      throw new BridgeError('stale_revision', '曲库已变化')
    })
    await store.selectMotion('hikari-no-saki', true)
    expect(store.getState().actionError).toContain('曲库已变化')
    bridge.close()
  })

  it('deletes through the confirmation flow with a revision guard', async () => {
    const { bridge, store } = await startedStore()
    store.requestDelete('encore-demo')
    expect(store.getState().pendingDelete?.key).toBe('encore-demo')
    expect(store.deleteGuardChanged()).toBe(false)
    await store.confirmDelete()
    expect(store.getState().pendingDelete).toBeNull()
    expect(store.getState().library.entries.some((entry) => entry.id === 'encore-demo')).toBe(false)
    bridge.close()
  })

  it('blocks deletion when the library changed after the dialog opened', async () => {
    const { bridge, store } = await startedStore()
    store.requestDelete('encore-demo')
    await bridge.request('library.delete', { key: 'practice-room', revision: store.getState().library.revision })
    expect(store.deleteGuardChanged()).toBe(true)
    const spy = vi.spyOn(bridge, 'request')
    await store.confirmDelete()
    expect(spy).not.toHaveBeenCalled()
    expect(store.getState().pendingDelete).not.toBeNull()
    bridge.close()
  })

  it('marks the dialog stale when native rejects with stale_revision', async () => {
    const { bridge, store } = await startedStore()
    store.requestDelete('encore-demo')
    vi.spyOn(bridge, 'request').mockImplementationOnce(async () => {
      throw new BridgeError('stale_revision', '曲库已变化')
    })
    await store.confirmDelete()
    expect(store.getState().deleteStale).toBe(true)
    expect(store.deleteGuardChanged()).toBe(true)
    bridge.close()
  })
})

describe('MotionStore camera and stage', () => {
  it('runs the draft-expected-pending camera acknowledgement', async () => {
    const { bridge, store } = await startedStore()
    const promise = store.setCameraQuickMode(2)
    expect(store.getState().cameraPending).toBe(true)
    await promise
    // The mock echoes camera.changed with the applied settings.
    expect(store.getState().cameraPending).toBe(false)
    expect(store.getState().camera.settings.mode).toBe(2)
    expect(store.getState().camera.settings.followBehavior).toBe(1)
    expect(store.cameraQuickModeIndex()).toBe(2)
    bridge.close()
  })

  it('keeps the lighting→camera shadow linkage via stage.set', async () => {
    const { bridge, store } = await startedStore()
    store.patchStage({ lightingEffectsEnabled: false })
    await new Promise((resolve) => setTimeout(resolve, 30))
    expect(store.getState().stage.settings.lightingEffectsEnabled).toBe(false)
    expect(store.getState().camera.settings.actorShadowExperimentMode).toBe(2)
    store.patchStage({ lightingEffectsEnabled: true })
    await new Promise((resolve) => setTimeout(resolve, 30))
    expect(store.getState().camera.settings.actorShadowExperimentMode).toBe(1)
    bridge.close()
  })

  it('selects actors with token+epoch and reports stale rosters', async () => {
    const { bridge, store } = await startedStore()
    await store.selectActor(102)
    expect(store.getState().actors.targetLabel).toBe('凛')
    vi.spyOn(bridge, 'request').mockImplementationOnce(async () => {
      throw new BridgeError('stale_token', '角色名单已变化')
    })
    await store.selectActor(101)
    expect(store.getState().actionError).toContain('角色名单已变化')
    bridge.close()
  })
})
