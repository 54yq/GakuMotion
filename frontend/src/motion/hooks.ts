import * as React from 'react'
import { getMotionBridge } from './bridge'
import type { MotionAppearance } from './protocol'
import { motionStore } from './store'
import { validTimeline } from './time'

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
  if (target.tagName === 'TEXTAREA' || target.tagName === 'SELECT') return true
  if (target.tagName === 'INPUT') {
    const type = (target as HTMLInputElement).type
    return !['button', 'checkbox', 'radio', 'range', 'submit', 'reset', 'file', 'color'].includes(type)
  }
  return target.isContentEditable
}

function isInteractiveControl(target: EventTarget | null): boolean {
  if (!(target instanceof HTMLElement)) return false
  if (isEditable(target)) return true
  return (
    target.closest(
      'button, a, select, input, textarea, [role="button"], [role="slider"], [role="switch"], [role="checkbox"], [role="combobox"], [role="tab"], [role="menuitem"], [role="option"], [role="listbox"]',
    ) !== null
  )
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
    const onWindowBlur = () => report(false)
    document.addEventListener('focusin', onFocusIn)
    document.addEventListener('focusout', onFocusOut)
    window.addEventListener('blur', onWindowBlur)
    return () => {
      document.removeEventListener('focusin', onFocusIn)
      document.removeEventListener('focusout', onFocusOut)
      window.removeEventListener('blur', onWindowBlur)
      report(false)
    }
  }, [enabled])
}

// Panel-local transport and navigation shortcuts. Native Win32 hotkeys
// (F9/F10/,/./-/=/...) are handled in C++ when running inside WebView2, so we
// never duplicate them in webview2 mode and only mirror them in standalone mock mode.
export function useMotionKeyboardShortcuts(enabled: boolean): void {
  React.useEffect(() => {
    if (!enabled) return
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.defaultPrevented) return
      const state = motionStore.getState()
      if (!state.connected || !state.permission.injectionAllowed) return
      if (state.pendingDelete !== null || state.importForm.open || state.importUi.visible || state.importUi.errorModalOpen) {
        return
      }

      const target = event.target
      if (isEditable(target)) return

      // Search focus shortcut: Ctrl+K (any page) or '/' (on library page)
      if ((event.ctrlKey || event.metaKey) && !event.altKey && event.key.toLowerCase() === 'k') {
        event.preventDefault()
        if (state.page !== 'library') motionStore.setPage('library')
        requestAnimationFrame(() => {
          const input = document.querySelector<HTMLInputElement>('input[aria-label="搜索曲目"]')
          input?.focus()
          input?.select()
        })
        return
      }
      if (event.key === '/' && !event.ctrlKey && !event.metaKey && !event.altKey && state.page === 'library') {
        event.preventDefault()
        const input = document.querySelector<HTMLInputElement>('input[aria-label="搜索曲目"]')
        input?.focus()
        return
      }

      const hasClip = validTimeline(state.player.totalFrames, state.player.fps)
      const supports = (method: string) => state.capabilities.includes(method)

      // Standalone mock preview mirrors native F9 / F10 / - / = shortcuts.
      if (state.bridgeKind === 'mock' && !event.ctrlKey && !event.metaKey && !event.altKey) {
        if (event.key === 'F9' && hasClip && supports('player.play')) {
          event.preventDefault()
          void (state.player.state === 'playing' ? motionStore.pause() : motionStore.play())
          return
        }
        if (event.key === 'F10' && hasClip && supports('player.stop')) {
          event.preventDefault()
          void motionStore.stop()
          return
        }
        if ((event.key === '-' || event.key === '=') && hasClip && supports('player.seek') && !isInteractiveControl(target)) {
          event.preventDefault()
          void motionStore.seekRelative(event.key === '-' ? -5 : 5)
          return
        }
      }

      if (isInteractiveControl(target)) return

      if (event.code === 'Space' && !event.ctrlKey && !event.metaKey && !event.altKey && !event.shiftKey) {
        if (hasClip && supports('player.play')) {
          event.preventDefault()
          void (state.player.state === 'playing' ? motionStore.pause() : motionStore.play())
        }
        return
      }

      if ((event.key === 'ArrowLeft' || event.key === 'ArrowRight') && !event.ctrlKey && !event.metaKey && !event.altKey) {
        if (!hasClip) return
        const dir = event.key === 'ArrowRight' ? 1 : -1
        if (event.shiftKey && supports('player.stepFrame')) {
          event.preventDefault()
          void motionStore.stepFrame(dir)
        } else if (!event.shiftKey && supports('player.seek')) {
          event.preventDefault()
          void motionStore.seekRelative(dir * state.seekStepSeconds)
        }
      }
    }

    document.addEventListener('keydown', onKeyDown)
    return () => document.removeEventListener('keydown', onKeyDown)
  }, [enabled])
}
