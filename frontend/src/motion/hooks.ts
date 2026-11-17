import * as React from 'react'
import { getMotionBridge } from './bridge'
import type { MotionAppearance } from './protocol'

const THEME_NAMES = ['dark', 'sapphire', 'paper', 'apple-light', 'apple-dark'] as const
const DARK_THEMES = new Set([0, 1, 4])

// Applies the host appearance (forwarded by the web-panel service) when
// available. DPI scaling is handled by WebView2; uiScale is the CSS zoom.
export function useMotionAppearanceEffect(appearance: MotionAppearance | null): void {
  React.useEffect(() => {
    const theme = appearance?.theme ?? 0
    const root = document.documentElement
    root.dataset.theme = THEME_NAMES[theme] ?? 'dark'
    root.classList.toggle('dark', DARK_THEMES.has(theme))
    root.style.zoom = String(appearance?.uiScale ?? 1)
  }, [appearance])
}

function isEditable(target: EventTarget | null): boolean {
  if (!(target instanceof HTMLElement)) return false
  if (target.tagName === 'TEXTAREA') return true
  if (target.tagName === 'INPUT') {
    const type = (target as HTMLInputElement).type
    return !['button', 'checkbox', 'radio', 'range', 'submit', 'reset', 'file', 'color'].includes(type)
  }
  return target.isContentEditable
}

// Reports editable focus so the native hotkey poller suppresses plugin hotkeys
// while typing, exactly like ImGui text input (WantsTextInput).
export function useMotionTextInputBridge(enabled: boolean): void {
  React.useEffect(() => {
    if (!enabled) return
    const bridge = getMotionBridge()
    if (!bridge) return
    let active = false
    const report = (focused: boolean) => {
      if (focused === active) return
      active = focused
      void bridge.request('shell.setTextInputActive', { active: focused }).catch(() => {})
    }
    const onFocusIn = (event: FocusEvent) => report(isEditable(event.target))
    const onFocusOut = (event: FocusEvent) => report(isEditable(event.relatedTarget))
    document.addEventListener('focusin', onFocusIn)
    document.addEventListener('focusout', onFocusOut)
    return () => {
      document.removeEventListener('focusin', onFocusIn)
      document.removeEventListener('focusout', onFocusOut)
      report(false)
    }
  }, [enabled])
}
