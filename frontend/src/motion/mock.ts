// DEV-ONLY mock of the GakuMotion web-panel bridge. This is NOT evidence of
// native integration: it exists so `npm run dev` and component tests are
// interactive without the game. The UI labels mock mode with a banner, and
// methods outside SUPPORTED_METHODS stay "待迁移/不可用" even here.

import {
  BridgeError,
  DEFAULT_CAMERA_SETTINGS,
  DEFAULT_FACIAL_WEIGHTS,
  DEFAULT_PLAYER_STATE,
  DEFAULT_STAGE_SETTINGS,
  type CameraSettings,
  type EnsembleSnapshot,
  type LibraryEntry,
  type MotionBridgeClient,
  type MotionEventName,
  type MotionEvents,
  type MotionMethodName,
  type MotionMethods,
  type MotionStateSnapshot,
  type PlayerState,
  type RequestOptions,
} from './protocol'
import { clamp } from './utils'
import { steppedFrame } from './time'

const SUPPORTED_METHODS: MotionMethodName[] = [
  'bridge.hello',
  'bridge.ping',
  'motion.hello',
  'state.get',
  'player.play',
  'player.pause',
  'player.resume',
  'player.stop',
  'player.cycle',
  'player.seek',
  'player.seekFrame',
  'player.stepFrame',
  'player.scrubBegin',
  'player.scrubUpdate',
  'player.scrubEnd',
  'player.setSpeed',
  'player.setVolume',
  'player.saveVolume',
  'player.reload',
  'playlist.setMode',
  'library.refresh',
  'library.select',
  'library.delete',
  'library.openFolder',
  'actors.selectTarget',
  'facial.setWeights',
  'facial.save',
  'position.set',
  'position.save',
  'camera.set',
  'camera.save',
  'stage.set',
  'settings.setPhysics',
  'shell.setTextInputActive',
  'appearance.get',
]

const SUPPORTED_EVENTS: MotionEventName[] = [
  'player.stateChanged',
  'permission.changed',
  'library.changed',
  'loaded.changed',
  'playlist.changed',
  'actors.changed',
  'camera.changed',
  'stage.changed',
  'facial.changed',
  'position.changed',
  'ensemble.changed',
  'settings.changed',
  'appearance.changed',
  'bridge.closed',
]

function seedEntries(): LibraryEntry[] {
  return [
    { id: 'hikari-no-saki', title: '輝きの先へ', directory: 'gakumas-local/motions/hikari-no-saki', durationSeconds: 212, totalFrames: 12720, hasMotion: true, hasFacial: true, hasCamera: true, hasAudio: true },
    { id: 'starlight-step', title: 'Starlight Step', directory: 'gakumas-local/motions/starlight-step', durationSeconds: 185.5, totalFrames: 11130, hasMotion: true, hasFacial: false, hasCamera: true, hasAudio: true },
    { id: 'aoi-haru', title: '青い春', directory: 'gakumas-local/motions/aoi-haru', durationSeconds: 240, totalFrames: 14400, hasMotion: true, hasFacial: true, hasCamera: false, hasAudio: true },
    { id: 'practice-room', title: '练习室镜像', directory: 'gakumas-local/motions/practice-room', durationSeconds: 96, totalFrames: 5760, hasMotion: true, hasFacial: false, hasCamera: false, hasAudio: false },
    { id: 'encore-demo', title: 'Encore（演示）', directory: 'gakumas-local/motions/encore-demo', durationSeconds: 30, totalFrames: 1800, hasMotion: true, hasFacial: true, hasCamera: true, hasAudio: false },
  ]
}

function seedEnsemble(): EnsembleSnapshot {
  return {
    settings: {
      enabled: false,
      masterSlot: 0,
      cameraSlot: 0,
      slots: [0, 1, 2].map(() => ({
        enabled: false,
        actorToken: 0,
        actorHint: '',
        motionKey: '',
        offset: { x: 0, y: 0, z: 0 },
        yawDegrees: 0,
      })),
    },
    revision: 0,
    actorEpoch: 1,
    available: false,
    verified: false,
    probeEnabled: false,
    active: false,
    notice: '',
    physics: ['', '', ''],
  }
}

export class MotionMockBridgeClient implements MotionBridgeClient {
  readonly kind = 'mock' as const
  private readonly handlers = new Map<MotionEventName, Set<(payload: never) => void>>()
  private appearanceState = { theme: 0, uiScale: 1, dpiScale: 1, scaleLimited: false }
  private closed = false
  private sessionId = 1
  private seq = 0
  private tickTimer: ReturnType<typeof setInterval> | null = null
  private refreshTimer: ReturnType<typeof setTimeout> | null = null
  private scrubbing = false

  private permission = { injectionAllowed: true, notice: '' }
  private player: PlayerState = { ...DEFAULT_PLAYER_STATE }
  private entries = seedEntries()
  private libraryRevision = 1
  private libraryNotice = ''
  private refreshing = false
  private loaded = { motionKey: '', generation: 0, injectionSession: 1, sourceMissing: false, sourceChanged: false }
  private playlist = { mode: 1, notice: '' }
  private actors = {
    epoch: 1,
    actors: [
      { token: 101, label: '咲季', selected: true },
      { token: 102, label: '凛', selected: false },
    ],
    targetLabel: '咲季',
    boundLabel: '咲季',
  }
  private camera = {
    settings: { ...DEFAULT_CAMERA_SETTINGS },
    available: true,
    owned: false,
    hasTrack: true,
    error: '',
    followTargetError: '',
    notice: '',
  }
  private stage = { settings: { ...DEFAULT_STAGE_SETTINGS }, sceneName: '摄影棚 A', statusNotice: '' }
  private facial = { weights: { ...DEFAULT_FACIAL_WEIGHTS }, notice: '' }
  private position = { offset: { x: 0, y: 0, z: 0 }, notice: '' }
  private ensemble = seedEnsemble()
  private physicsBackend = 2
  private volumeSaved = 1

  private emit<E extends MotionEventName>(event: E, payload: MotionEvents[E]): void {
    this.seq += 1
    const set = this.handlers.get(event)
    if (set) for (const handler of set) (handler as (p: unknown) => void)(payload)
  }

  // Test/dev helper to push a native-style event.
  push<E extends MotionEventName>(event: E, payload: MotionEvents[E]): void {
    this.emit(event, payload)
  }

  private emitPlayer(): void {
    this.emit('player.stateChanged', { ...this.player })
  }

  private emitLibrary(): void {
    this.emit('library.changed', {
      revision: this.libraryRevision,
      entries: this.entries.map((entry) => ({ ...entry })),
      refreshing: this.refreshing,
      notice: this.libraryNotice,
    })
  }

  private startTick(): void {
    if (this.tickTimer) return
    this.tickTimer = setInterval(() => {
      if (this.closed || this.scrubbing || this.player.state !== 'playing') return
      const next = this.player.time + 0.1 * this.player.speed
      if (next >= this.player.duration) {
        if (this.player.playMode === 1) {
          this.player = { ...this.player, time: 0 }
        } else if (this.player.playMode === 2 || this.player.playMode === 3) {
          this.cycle(this.player.playMode === 3 ? 'random' : 1)
          return
        } else {
          this.player = { ...this.player, state: 'stopped', time: 0 }
        }
      } else {
        this.player = { ...this.player, time: next }
      }
      this.emitPlayer()
    }, 100)
  }

  private loadEntry(entry: LibraryEntry, autoplay: boolean): void {
    this.loaded = {
      motionKey: entry.id,
      generation: this.loaded.generation + 1,
      injectionSession: this.loaded.injectionSession,
      sourceMissing: false,
      sourceChanged: false,
    }
    this.player = {
      ...this.player,
      state: autoplay ? 'playing' : 'preview',
      title: entry.title,
      time: 0,
      duration: entry.durationSeconds,
      totalFrames: entry.totalFrames,
      fps: entry.totalFrames > 0 && entry.durationSeconds > 0 ? entry.totalFrames / entry.durationSeconds : 60,
    }
    this.camera = { ...this.camera, hasTrack: entry.hasCamera }
    this.emit('loaded.changed', { ...this.loaded })
    this.emit('camera.changed', { ...this.camera, settings: { ...this.camera.settings } })
    this.emitPlayer()
    if (autoplay) this.startTick()
  }

  private cycle(direction: number | 'random'): void {
    if (this.entries.length === 0) return
    const currentIndex = this.entries.findIndex((entry) => entry.id === this.loaded.motionKey)
    let nextIndex: number
    if (direction === 'random') nextIndex = Math.floor(Math.random() * this.entries.length)
    else nextIndex = (currentIndex + direction + this.entries.length) % this.entries.length
    this.loadEntry(this.entries[nextIndex], true)
  }

  private requireClip(): void {
    if (this.player.totalFrames <= 0 || this.player.duration <= 0) {
      throw new BridgeError('not_ready', '尚未加载曲目')
    }
  }

  private requireAllowed(): void {
    if (!this.permission.injectionAllowed) throw new BridgeError('not_allowed', 'GakuMotion 当前已禁用')
  }

  async request<M extends MotionMethodName>(
    method: M,
    params?: MotionMethods[M]['params'],
    _options?: RequestOptions,
  ): Promise<MotionMethods[M]['result']> {
    if (this.closed) throw new BridgeError('closed', 'The bridge is closed')
    await new Promise((resolve) => setTimeout(resolve, 5))
    const p = (params ?? {}) as Record<string, unknown>
    switch (method) {
      case 'bridge.hello':
        return { protocol: 1, sessionId: this.sessionId } as MotionMethods[M]['result']
      case 'bridge.ping':
        return { pong: true } as MotionMethods[M]['result']
      case 'motion.hello':
        return {
          protocol: 1,
          sessionId: this.sessionId,
          pluginVersion: '0.1.1-mock',
          supportedMethods: [...SUPPORTED_METHODS],
          supportedEvents: [...SUPPORTED_EVENTS],
        } as MotionMethods[M]['result']
      case 'state.get':
        return this.snapshot() as MotionMethods[M]['result']
      case 'player.play':
        this.requireAllowed()
        this.requireClip()
        this.player = { ...this.player, state: 'playing' }
        this.startTick()
        this.emitPlayer()
        return {} as MotionMethods[M]['result']
      case 'player.pause':
        this.requireAllowed()
        if (this.player.state === 'playing') this.player = { ...this.player, state: 'paused' }
        this.emitPlayer()
        return {} as MotionMethods[M]['result']
      case 'player.resume':
        this.requireAllowed()
        this.requireClip()
        if (this.player.state === 'paused' || this.player.state === 'preview') {
          this.player = { ...this.player, state: 'playing' }
          this.startTick()
        }
        this.emitPlayer()
        return {} as MotionMethods[M]['result']
      case 'player.stop':
        this.requireAllowed()
        this.player = { ...this.player, state: 'stopped', time: 0 }
        this.emitPlayer()
        return {} as MotionMethods[M]['result']
      case 'player.cycle': {
        this.requireAllowed()
        if (this.ensemble.active) throw new BridgeError('protected', '多人舞台激活时不能切换曲目')
        this.cycle(Number(p.delta) >= 0 ? 1 : -1)
        return {} as MotionMethods[M]['result']
      }
      case 'player.seek': {
        this.requireAllowed()
        this.requireClip()
        const seconds = Number(p.seconds)
        if (!Number.isFinite(seconds) || seconds < 0 || seconds > this.player.duration) {
          throw new BridgeError('invalid_params', '时间超出范围')
        }
        this.player = { ...this.player, time: seconds }
        this.emitPlayer()
        return {} as MotionMethods[M]['result']
      }
      case 'player.seekFrame': {
        this.requireAllowed()
        this.requireClip()
        const frame = Number(p.frame)
        if (!Number.isInteger(frame) || frame < 0 || frame >= this.player.totalFrames) {
          throw new BridgeError('invalid_params', '帧号超出范围；末帧为总采样帧数减 1')
        }
        this.player = { ...this.player, state: 'paused', time: frame / this.player.fps }
        this.emitPlayer()
        return {} as MotionMethods[M]['result']
      }
      case 'player.stepFrame': {
        this.requireAllowed()
        this.requireClip()
        const delta = Number(p.delta) >= 0 ? 1 : -1
        const frame = steppedFrame(this.player.time, this.player.fps, this.player.totalFrames, delta)
        this.player = { ...this.player, state: 'paused', time: frame / this.player.fps }
        this.emitPlayer()
        return {} as MotionMethods[M]['result']
      }
      case 'player.scrubBegin':
        this.requireAllowed()
        this.requireClip()
        this.scrubbing = true
        this.player = { ...this.player, time: clamp(Number(p.seconds), 0, this.player.duration) }
        this.emitPlayer()
        return {} as MotionMethods[M]['result']
      case 'player.scrubUpdate':
        this.requireAllowed()
        this.player = { ...this.player, time: clamp(Number(p.seconds), 0, this.player.duration) }
        this.emitPlayer()
        return {} as MotionMethods[M]['result']
      case 'player.scrubEnd':
        this.requireAllowed()
        this.scrubbing = false
        this.player = { ...this.player, time: clamp(Number(p.seconds), 0, this.player.duration) }
        this.emitPlayer()
        return {} as MotionMethods[M]['result']
      case 'player.setSpeed':
        this.player = { ...this.player, speed: clamp(Number(p.speed), 0.1, 3) }
        this.emitPlayer()
        return {} as MotionMethods[M]['result']
      case 'player.setVolume':
        this.player = { ...this.player, volume: clamp(Number(p.volume), 0, 1) }
        this.emitPlayer()
        return {} as MotionMethods[M]['result']
      case 'player.saveVolume':
        this.volumeSaved = this.player.volume
        return {} as MotionMethods[M]['result']
      case 'player.reload': {
        this.requireAllowed()
        const entry = this.entries.find((candidate) => candidate.id === this.loaded.motionKey)
        if (!entry) throw new BridgeError('not_ready', '没有已加载曲目')
        const time = this.player.time
        this.loadEntry(entry, false)
        this.player = { ...this.player, state: 'paused', time }
        this.emitPlayer()
        return {} as MotionMethods[M]['result']
      }
      case 'playlist.setMode': {
        const mode = Number(p.mode)
        this.playlist = { ...this.playlist, mode: mode >= 0 && mode <= 3 ? mode : 1 }
        this.player = { ...this.player, playMode: this.playlist.mode }
        this.emit('playlist.changed', { ...this.playlist })
        this.emitPlayer()
        return {} as MotionMethods[M]['result']
      }
      case 'library.refresh': {
        if (this.refreshing) throw new BridgeError('busy', '正在刷新曲库')
        this.refreshing = true
        this.emitLibrary()
        this.refreshTimer = setTimeout(() => {
          this.refreshing = false
          this.emitLibrary()
        }, 250)
        return { started: true } as MotionMethods[M]['result']
      }
      case 'library.select': {
        this.requireAllowed()
        if (Number(p.revision) !== this.libraryRevision) throw new BridgeError('stale_revision', '曲库已变化，请刷新后重试')
        const entry = this.entries.find((candidate) => candidate.id === p.key)
        if (!entry) throw new BridgeError('invalid_params', '曲目不存在')
        this.loadEntry(entry, Boolean(p.autoplay))
        return { key: entry.id } as MotionMethods[M]['result']
      }
      case 'library.delete': {
        this.requireAllowed()
        if (this.refreshing) throw new BridgeError('busy', '曲库刷新中')
        if (Number(p.revision) !== this.libraryRevision) throw new BridgeError('stale_revision', '曲库已变化，请取消后重新选择曲目')
        const index = this.entries.findIndex((candidate) => candidate.id === p.key)
        if (index < 0) throw new BridgeError('invalid_params', '曲目不存在')
        this.entries.splice(index, 1)
        this.libraryRevision += 1
        if (this.loaded.motionKey === p.key) {
          this.loaded = { ...this.loaded, motionKey: '', generation: this.loaded.generation + 1 }
          this.player = { ...DEFAULT_PLAYER_STATE, volume: this.player.volume, speed: this.player.speed, playMode: this.player.playMode }
          this.emit('loaded.changed', { ...this.loaded })
          this.emitPlayer()
        }
        this.emitLibrary()
        return {} as MotionMethods[M]['result']
      }
      case 'library.openFolder':
        return {} as MotionMethods[M]['result']
      case 'actors.selectTarget': {
        this.requireAllowed()
        if (Number(p.epoch) !== this.actors.epoch) throw new BridgeError('stale_token', '角色名单已变化')
        const actor = this.actors.actors.find((candidate) => candidate.token === Number(p.token))
        if (!actor) throw new BridgeError('stale_token', '角色不存在')
        this.actors = {
          ...this.actors,
          actors: this.actors.actors.map((candidate) => ({ ...candidate, selected: candidate.token === actor.token })),
          targetLabel: actor.label,
        }
        this.emit('actors.changed', { ...this.actors, actors: this.actors.actors.map((a) => ({ ...a })) })
        return {} as MotionMethods[M]['result']
      }
      case 'facial.setWeights': {
        this.requireAllowed()
        this.facial = {
          weights: {
            overall: clamp(Number(p.overall), 0, 2),
            brow: clamp(Number(p.brow), 0, 2),
            eye: clamp(Number(p.eye), 0, 2),
            mouth: clamp(Number(p.mouth), 0, 2),
          },
          notice: '',
        }
        this.emit('facial.changed', { weights: { ...this.facial.weights }, notice: '' })
        return {} as MotionMethods[M]['result']
      }
      case 'facial.save':
        this.facial = { ...this.facial, notice: '已保存表情幅度（mock）' }
        this.emit('facial.changed', { weights: { ...this.facial.weights }, notice: this.facial.notice })
        return {} as MotionMethods[M]['result']
      case 'position.set': {
        this.requireAllowed()
        this.position = {
          offset: { x: clamp(Number(p.x), -10, 10), y: clamp(Number(p.y), -10, 10), z: clamp(Number(p.z), -10, 10) },
          notice: '',
        }
        this.emit('position.changed', { offset: { ...this.position.offset }, notice: '' })
        return {} as MotionMethods[M]['result']
      }
      case 'position.save':
        this.position = { ...this.position, notice: '已保存初始位置为默认（mock）' }
        this.emit('position.changed', { offset: { ...this.position.offset }, notice: this.position.notice })
        return {} as MotionMethods[M]['result']
      case 'camera.set': {
        this.requireAllowed()
        this.camera = { ...this.camera, settings: { ...(p.settings as CameraSettings) }, owned: (p.settings as CameraSettings).enabled }
        this.emit('camera.changed', { ...this.camera, settings: { ...this.camera.settings } })
        return {} as MotionMethods[M]['result']
      }
      case 'camera.save':
        this.camera = { ...this.camera, notice: '已保存镜头设置（mock）' }
        this.emit('camera.changed', { ...this.camera, settings: { ...this.camera.settings } })
        return {} as MotionMethods[M]['result']
      case 'stage.set': {
        this.requireAllowed()
        const settings = { ...(p.settings as MotionStateSnapshot['stage']['settings']) }
        this.stage = { ...this.stage, settings }
        this.emit('stage.changed', { ...this.stage, settings: { ...settings } })
        // Native-owned cross-domain linkage (ImGui did this in the UI layer).
        const shadowMode = settings.lightingEffectsEnabled ? 1 : 2
        if (this.camera.settings.actorShadowExperimentMode !== shadowMode) {
          this.camera = {
            ...this.camera,
            settings: { ...this.camera.settings, actorShadowExperimentMode: shadowMode },
          }
          this.emit('camera.changed', { ...this.camera, settings: { ...this.camera.settings } })
        }
        return {} as MotionMethods[M]['result']
      }
      case 'settings.setPhysics': {
        const backend = Number(p.backend)
        if (backend < 0 || backend > 2) throw new BridgeError('invalid_params', '无效的物理后端')
        this.physicsBackend = backend
        this.emit('settings.changed', { physicsBackend: backend })
        return {} as MotionMethods[M]['result']
      }
      case 'shell.setTextInputActive':
        return {} as MotionMethods[M]['result']
      case 'appearance.get':
        return { ...this.appearanceState } as MotionMethods[M]['result']
      default:
        throw new BridgeError('not_implemented', `待迁移：${method} 尚未接入原生实现`)
    }
  }

  snapshot(): MotionStateSnapshot {
    return {
      permission: { ...this.permission },
      player: { ...this.player },
      library: {
        revision: this.libraryRevision,
        entries: this.entries.map((entry) => ({ ...entry })),
        refreshing: this.refreshing,
        notice: this.libraryNotice,
      },
      loaded: { ...this.loaded },
      playlist: { ...this.playlist },
      actors: { ...this.actors, actors: this.actors.actors.map((actor) => ({ ...actor })) },
      camera: { ...this.camera, settings: { ...this.camera.settings } },
      stage: { ...this.stage, settings: { ...this.stage.settings } },
      facial: { weights: { ...this.facial.weights }, notice: this.facial.notice },
      position: { offset: { ...this.position.offset }, notice: this.position.notice },
      ensemble: { ...this.ensemble, settings: { ...this.ensemble.settings, slots: this.ensemble.settings.slots.map((slot) => ({ ...slot, offset: { ...slot.offset } })) } },
      physicsBackend: this.physicsBackend,
      musicVolumeSupport: 0,
      musicVolumeNotice: '',
      importProgress: null,
      motionsRoot: 'gakumas-local/motions',
    }
  }

  // Dev/test helpers to exercise permission, appearance, and session flows.
  setPermission(injectionAllowed: boolean, notice = ''): void {
    this.permission = { injectionAllowed, notice }
    if (!injectionAllowed) this.scrubbing = false
    this.emit('permission.changed', { ...this.permission })
  }

  setAppearance(theme: number, uiScale: number): void {
    this.appearanceState = { ...this.appearanceState, theme, uiScale }
    this.emit('appearance.changed', { ...this.appearanceState })
  }

  savedVolume(): number {
    return this.volumeSaved
  }

  on<E extends MotionEventName>(event: E, handler: (payload: MotionEvents[E]) => void): () => void {
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
    this.closed = true
    if (this.tickTimer) clearInterval(this.tickTimer)
    if (this.refreshTimer) clearTimeout(this.refreshTimer)
    this.tickTimer = null
    this.refreshTimer = null
  }
}
