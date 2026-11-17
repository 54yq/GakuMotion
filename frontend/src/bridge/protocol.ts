export const PROTOCOL_VERSION = 1

export class BridgeError extends Error {
  readonly code: string
  readonly details?: unknown
  constructor(code: string, message: string, details?: unknown) {
    super(message)
    this.name = 'BridgeError'
    this.code = code
    this.details = details
  }
}

export interface RequestOptions {
  signal?: AbortSignal
  timeoutMs?: number
}
