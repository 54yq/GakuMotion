// Port of GuiCameraMode.hpp. CameraMode: Track=0, Hold=1(legacy), Follow=2.
// FollowBehavior: LookAt=0, FollowMove=1.

import type { CameraSettings } from './protocol'

export function cameraControlModeIndex(settings: CameraSettings): number {
  if (!settings.enabled) return 0
  if (settings.mode !== 2) return 1
  return settings.followBehavior === 0 ? 3 : 2
}

export function setCameraControlMode(settings: CameraSettings, selection: number): CameraSettings {
  if (selection < 0 || selection > 3) return settings
  const next: CameraSettings = { ...settings, enabled: selection !== 0 }
  // Returning to the game's camera releases ownership and retains GM parameters.
  if (selection === 0) return next
  next.mode = selection === 1 ? 0 : 2
  next.followActor = selection !== 1
  if (selection === 2) next.followBehavior = 1
  if (selection === 3) next.followBehavior = 0
  return next
}

// Legacy offsets are mirrored into per-mode transforms until the user edits one,
// then the settings upgrade to usePerModeTransforms (same as the ImGui page).
export function syncLegacyTransforms(settings: CameraSettings): CameraSettings {
  if (settings.usePerModeTransforms) return settings
  return {
    ...settings,
    transformTrack: { ...settings.transformTrack, position: { ...settings.vmdOffset } },
    transformFollowMove: { ...settings.transformFollowMove, position: { ...settings.followOffset } },
    transformLookAt: { ...settings.transformLookAt, position: { ...settings.followOffset } },
  }
}

export function upgradePerModeTransforms(settings: CameraSettings): CameraSettings {
  if (settings.usePerModeTransforms) return settings
  return { ...syncLegacyTransforms(settings), usePerModeTransforms: true }
}
