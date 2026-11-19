import { BridgeError, PROTOCOL_VERSION, type RequestOptions } from './protocol'

export interface ChromeWebview {
  postMessage(message: unknown): void
  addEventListener(type: 'message', listener: (event: { data: unknown }) => void): void
  removeEventListener(type: 'message', listener: (event: { data: unknown }) => void): void
}

declare global {
  interface Window {
    chrome?: { webview?: ChromeWebview }
  }
}

export function isWebView2Available(): boolean {
  return typeof window !== 'undefined' && !!window.chrome?.webview
}

interface Pending {
  method: string
  resolve: (value: unknown) => void
  reject: (error: BridgeError) => void
  timer: ReturnType<typeof setTimeout>
  cleanup: () => void
}

interface WireResponse {
  v: number
  type: 'response'
  id: number
  ok: boolean
  result?: unknown
  error?: { code: string; message: string; details?: unknown }
}

interface WireEvent {
  v: number
  type: 'event'
  name: string
  payload: unknown
  seq: number
  sid: number
}

// Generic request/response + event transport over window.chrome.webview.
// Correlates by id, rejects pending work on shutdown/session reset, and drops
// stale-session events so a React refresh never resolves against a previous
// native session.
export class WebView2BridgeCore<
  Methods extends Record<keyof Methods, { params: unknown; result: unknown }>,
  Events extends Record<keyof Events, unknown>,
> {
  private readonly webview: ChromeWebview
  private readonly pending = new Map<number, Pending>()
  private readonly handlers = new Map<keyof Events, Set<(payload: never) => void>>()
  private nextId = 1
  private sessionId: number | null = null
  private closed = false
  private readonly listener = (event: { data: unknown }) => this.onMessage(event.data)
  private readonly onPageHide = () => this.close()

  constructor() {
    const webview = window.chrome?.webview
    if (!webview) throw new BridgeError('no_webview', 'window.chrome.webview is unavailable')
    this.webview = webview
    this.webview.addEventListener('message', this.listener)
    window.addEventListener('pagehide', this.onPageHide)
  }

  request<M extends keyof Methods & string>(
    method: M,
    params?: Methods[M]['params'],
    options?: RequestOptions,
  ): Promise<Methods[M]['result']> {
    if (this.closed) return Promise.reject(new BridgeError('closed', 'The bridge is closed'))
    return new Promise<Methods[M]['result']>((resolve, reject) => {
      const id = this.nextId++
      const signal = options?.signal
      const timer = setTimeout(() => {
        this.take(id)
        reject(new BridgeError('timeout', `Request timed out: ${method}`))
      }, options?.timeoutMs ?? 15000)
      const onAbort = () => {
        this.take(id)
        reject(new BridgeError('cancelled', `Request cancelled: ${method}`))
      }
      if (signal) {
        if (signal.aborted) {
          clearTimeout(timer)
          onAbort()
          return
        }
        signal.addEventListener('abort', onAbort, { once: true })
      }
      const cleanup = () => {
        clearTimeout(timer)
        if (signal) signal.removeEventListener('abort', onAbort)
      }
      this.pending.set(id, {
        method,
        resolve: resolve as (value: unknown) => void,
        reject,
        timer,
        cleanup,
      })
      try {
        this.webview.postMessage({ v: PROTOCOL_VERSION, id, type: 'request', method, params: params ?? {} })
      } catch (error) {
        this.take(id)
        reject(new BridgeError('transport_error', error instanceof Error ? error.message : String(error)))
      }
    })
  }

  on<E extends keyof Events & string>(event: E, handler: (payload: Events[E]) => void): () => void {
    let set = this.handlers.get(event)
    if (!set) {
      set = new Set()
      this.handlers.set(event, set)
    }
    set.add(handler as (payload: never) => void)
    return () => {
      set?.delete(handler as (payload: never) => void)
    }
  }

  close(): void {
    if (this.closed) return
    this.closed = true
    this.rejectAllPending('closed', 'The bridge is closed')
    this.webview.removeEventListener('message', this.listener)
    window.removeEventListener('pagehide', this.onPageHide)
  }

  private take(id: number): Pending | undefined {
    const entry = this.pending.get(id)
    if (entry) {
      entry.cleanup()
      this.pending.delete(id)
    }
    return entry
  }

  private rejectAllPending(code: string, message: string): void {
    for (const [id] of this.pending) {
      const entry = this.take(id)
      entry?.reject(new BridgeError(code, message))
    }
  }

  private onMessage(data: unknown): void {
    if (!data || typeof data !== 'object') return
    const message = data as WireResponse | WireEvent
    if (message.v !== PROTOCOL_VERSION) return
    if (message.type === 'response') {
      const entry = this.take(message.id)
      if (!entry) return
      if (entry.method === 'bridge.hello' && message.ok) {
        const result = message.result as { sessionId?: number }
        if (typeof result?.sessionId === 'number') this.sessionId = result.sessionId
      }
      if (message.ok) entry.resolve(message.result ?? {})
      else entry.reject(new BridgeError(message.error?.code ?? 'unknown', message.error?.message ?? 'Request failed', message.error?.details))
      return
    }
    if (message.type === 'event') {
      if (typeof message.sid === 'number') {
        if (this.sessionId === null) this.sessionId = message.sid
        else if (message.sid < this.sessionId) return // stale session
        else if (message.sid > this.sessionId) {
          this.sessionId = message.sid
          this.rejectAllPending('reset', 'A new native session started')
        }
      }
      if (message.name === 'bridge.closed') {
        this.closed = true
        const reason = (message.payload as { reason?: string })?.reason ?? 'closed'
        this.rejectAllPending(reason, 'The native bridge closed')
      }
      const set = this.handlers.get(message.name as keyof Events)
      if (set) for (const handler of set) (handler as (payload: unknown) => void)(message.payload)
    }
  }
}
