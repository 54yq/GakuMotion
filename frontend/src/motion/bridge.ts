import { WebView2BridgeCore, isWebView2Available } from '@/bridge/core'
import { MotionMockBridgeClient } from './mock'
import type { MotionBridgeClient, MotionEvents, MotionMethods } from './protocol'

export { isWebView2Available }

// Motion panel client: WebView2 wire transport bound to the GakuMotion
// web-panel allow-list (docs/motion-bridge-protocol.md).
export class MotionWebView2Client
  extends WebView2BridgeCore<MotionMethods, MotionEvents>
  implements MotionBridgeClient
{
  readonly kind = 'webview2' as const
}

let client: MotionBridgeClient | null = null

// Inside WebView2 the native service is used. In Vite dev / vitest / standalone
// preview mode (`vite --mode preview`) the mock bridge is available; a
// production build outside WebView2 has no bridge and shows an unavailable screen.
export function getMotionBridge(): MotionBridgeClient | null {
  if (!client) {
    if (isWebView2Available()) client = new MotionWebView2Client()
    else if (import.meta.env.DEV || import.meta.env.MODE === 'preview') client = new MotionMockBridgeClient()
    else return null
  }
  return client
}

export function setMotionBridge(next: MotionBridgeClient | null): void {
  client = next
}

export { MotionMockBridgeClient } from './mock'
