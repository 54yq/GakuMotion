import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { MotionWebView2Client } from '@/motion/bridge'
import { BridgeError } from '@/motion/protocol'

type Listener = (event: { data: unknown }) => void

class FakeWebview {
  posted: Record<string, unknown>[] = []
  private listeners = new Set<Listener>()
  postMessage(message: unknown) {
    this.posted.push(message as Record<string, unknown>)
  }
  addEventListener(_type: 'message', listener: Listener) {
    this.listeners.add(listener)
  }
  removeEventListener(_type: 'message', listener: Listener) {
    this.listeners.delete(listener)
  }
  emit(data: unknown) {
    for (const listener of this.listeners) listener({ data })
  }
}

let fake: FakeWebview

beforeEach(() => {
  fake = new FakeWebview()
  ;(window as unknown as { chrome: { webview: FakeWebview } }).chrome = { webview: fake }
})

afterEach(() => {
  delete (window as unknown as { chrome?: unknown }).chrome
  vi.useRealTimers()
})

describe('MotionWebView2Client', () => {
  it('posts the shared wire shape and correlates by id', async () => {
    const client = new MotionWebView2Client()
    const promise = client.request('player.seek', { seconds: 12.5 })
    expect(fake.posted[0]).toMatchObject({ v: 1, type: 'request', method: 'player.seek', params: { seconds: 12.5 } })
    const id = fake.posted[0].id as number
    fake.emit({ v: 1, type: 'response', id, ok: true, result: {} })
    await expect(promise).resolves.toEqual({})
    client.close()
  })

  it('rejects with the native error code', async () => {
    const client = new MotionWebView2Client()
    const promise = client.request('library.delete', { key: 'x', revision: 1 })
    const id = fake.posted[0].id as number
    fake.emit({ v: 1, type: 'response', id, ok: false, error: { code: 'stale_revision', message: '曲库已变化' } })
    await expect(promise).rejects.toMatchObject({ code: 'stale_revision' })
    client.close()
  })

  it('drops stale-session events and resets pending on a new session', async () => {
    const client = new MotionWebView2Client()
    const hello = client.request('bridge.hello')
    fake.emit({ v: 1, type: 'response', id: fake.posted[0].id as number, ok: true, result: { protocol: 1, sessionId: 5 } })
    await hello

    const seen: number[] = []
    client.on('player.stateChanged', (payload) => seen.push(payload.time))
    fake.emit({ v: 1, type: 'event', name: 'player.stateChanged', payload: { time: 1 }, seq: 1, sid: 4 })
    expect(seen).toEqual([])
    fake.emit({ v: 1, type: 'event', name: 'player.stateChanged', payload: { time: 2 }, seq: 2, sid: 5 })
    expect(seen).toEqual([2])

    const pending = client.request('state.get')
    fake.emit({ v: 1, type: 'event', name: 'player.stateChanged', payload: { time: 3 }, seq: 3, sid: 6 })
    await expect(pending).rejects.toMatchObject({ code: 'reset' })
    client.close()
  })

  it('invalidates pending work on bridge.closed', async () => {
    const client = new MotionWebView2Client()
    const pending = client.request('state.get')
    fake.emit({ v: 1, type: 'event', name: 'bridge.closed', payload: { reason: 'shutdown' }, seq: 1, sid: 1 })
    await expect(pending).rejects.toBeInstanceOf(BridgeError)
    await expect(client.request('bridge.ping')).rejects.toMatchObject({ code: 'closed' })
    client.close()
  })
})
