import { useEffect, useState } from 'react'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Vector3Field } from './NumberField'
import { getMotionBridge } from '../bridge'
import { useMotionStore } from '../store'
import type { ForkFollower, ForkState, Vector3 } from '../protocol'

const zero = { x: 0, y: 0, z: 0 }
const mockState: ForkState = {
  session: 1, owner: 1, epoch: 1, revision: 1, observed: true, viewing: true,
  paused: true, active: true, available: true, aspect: true, loop: false, loopLead: 1, liveTime: 42, duration: 155,
  liveNotice: 'Live 已暂停，可以为场景角色选择动作。',
  position: zero, yaw: 0, primaryPhysics: '原生物理运行中', formationNotice: '',
  placementReset: true, initializeBones: true, auxMode: 0, wristStatus: 0, armNotice: '',
  arms: [
    { token: 101, share: 0, degrees: 45, locked: false, bound: true, primary: true },
    { token: 102, share: 0.2, degrees: 45, locked: false, bound: true, primary: false },
  ],
  props: [
    { key: 'sample-mic', actorToken: 101, actor: '咲季', name: '麦克风', hidden: false, editable: true },
    { key: 'sample-light', actorToken: 102, actor: '凛', name: '荧光棒', hidden: true, editable: true },
  ],
  propsEditable: true, followYaw: false, liveDof: false, liveClock: false, liveClockAvailable: true,
  liveDiagnostics: false, liveClockNotice: '',
  liveFps: 59.8, liveStalls: 0, swingReportLoaded: false,
  folders: [], folderNotice: '',
  followers: [
    { slot: 0, actorToken: 102, generation: 1, motionKey: '', position: { x: -1.2, y: 0, z: 0.4 }, yaw: 12, offsetSeconds: 0, physics: '原生物理' },
    { slot: 1, actorToken: 103, generation: 1, motionKey: '', position: { x: 1.2, y: 0, z: 0.4 }, yaw: -12, offsetSeconds: 0, physics: '原生物理' },
  ],
}

export type ForkCommand = (action: string, fields?: Record<string, unknown>) => Promise<void>

export function useForkWorkspace(preview = false) {
  const supported = useMotionStore(state => state.capabilities.includes('fork.get'))
  const [state, setState] = useState<ForkState | null>(null)
  const [error, setError] = useState('')
  useEffect(() => {
    if (preview || !supported) return
    let disposed = false
    let pending = false
    const tick = async () => {
      const bridge = getMotionBridge()
      if (pending || !bridge) return
      pending = true
      try {
        const next = await bridge.request('fork.get')
        if (!disposed) setState(next)
      } catch {
        if (!disposed) setState(null)
      } finally { pending = false }
    }
    void tick()
    const timer = window.setInterval(() => void tick(), 450)
    return () => { disposed = true; window.clearInterval(timer) }
  }, [supported, preview])
  const current = supported || preview ? state ?? (preview ? mockState : null) : null
  const command: ForkCommand = async (action, fields = {}) => {
    if (!current) return
    setError('')
    if (preview) {
      if (action === 'live.takeover') setState({ ...current, active: Boolean(fields.value) })
      if (action === 'live.aspect') setState({ ...current, aspect: Boolean(fields.value) })
      if (action === 'live.loop') setState({ ...current, loop: Boolean(fields.value) })
      if (action === 'live.loopLead') setState({ ...current, loopLead: Number(fields.value) })
      if (action === 'live.pause') setState({ ...current, paused: Boolean(fields.value) })
      if (action === 'live.seek') setState({ ...current, liveTime: Number(fields.value) })
      if (action === 'motion.resetPhysics') setState({ ...current, placementReset: Boolean(fields.value) })
      if (action === 'motion.initializeBones') setState({ ...current, initializeBones: Boolean(fields.value) })
      if (action === 'motion.auxMode') setState({ ...current, auxMode: Number(fields.value) })
      if (action === 'motion.wristReport') setState({ ...current, wristStatus: 2 })
      if (action === 'camera.followYaw') setState({ ...current, followYaw: Boolean(fields.value) })
      if (action === 'live.dof') setState({ ...current, liveDof: Boolean(fields.value) })
      if (action === 'live.clock') setState({ ...current, liveClock: Boolean(fields.value) })
      if (action === 'live.armDiagnostics') setState({ ...current, liveDiagnostics: Boolean(fields.value) })
      if (action === 'live.swingReport') setState({ ...current, swingReportLoaded: true })
      if (action === 'stage.prop') setState({ ...current, props: current.props.map(item =>
        item.key === fields.key ? { ...item, hidden: Boolean(fields.value) } : item) })
      if (action === 'motion.arm') setState({ ...current, arms: current.arms.map(item => {
        if (item.token !== fields.actorToken) return item
        switch (fields.operation) {
          case 0: return { ...item, share: Number(fields.value) }
          case 1: return { ...item, degrees: Number(fields.value) }
          case 2: return { ...item, share: 0, degrees: 45 }
          case 3: return { ...item, locked: Boolean(fields.value) }
          default: return item
        }
      }) })
      if (action === 'formation.place') {
        const patch = {
          ...(fields.position ? { position: fields.position as Vector3 } : {}),
          ...(typeof fields.yaw === 'number' ? { yaw: fields.yaw } : {}),
        }
        setState(fields.slot === -1 ? { ...current, ...patch } : {
          ...current, followers: current.followers.map(item => item.slot === fields.slot ? { ...item, ...patch } : item),
        })
      }
      return
    }
    try {
      const bridge = getMotionBridge()
      if (!bridge) return
      await bridge.request('fork.command', { ...fields, action, session: current.session, owner: current.owner, epoch: current.epoch, revision: current.revision })
      setState(await bridge.request('fork.get'))
    } catch (cause) { setError(cause instanceof Error ? cause.message : String(cause)) }
  }
  return { state: current, command, error }
}

export function LiveControls({ state, command }: { state: ForkState; command: ForkCommand }) {
  const [seek, setSeek] = useState<number | null>(null)
  const canSeek = state.viewing && !state.active && state.duration > 0
  return (
    <section className="space-y-3 rounded-lg border bg-card p-4">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <div>
          <h2 className="text-sm font-semibold">Live 接管</h2>
          <p className="mt-1 text-xs text-muted-foreground">{state.liveNotice || (state.viewing ? '已识别普通 Live' : '等待 Live 场景就绪')}</p>
        </div>
        <Button size="sm" variant={state.active ? 'outline' : 'default'} disabled={!state.active && !state.available}
          onClick={() => void command('live.takeover', { value: !state.active })}>
          {state.active ? '退出接管并恢复 Live' : '启动 Live 接管'}
        </Button>
      </div>
      <div className="flex flex-wrap items-center gap-4">
        <Button size="sm" variant="secondary" disabled={!canSeek}
          onClick={() => void command('live.pause', { value: !state.paused })}>{state.paused ? '播放原 Live' : '暂停原 Live'}</Button>
        <label className="flex min-w-52 flex-1 items-center gap-2 text-xs text-muted-foreground">
          原 Live 进度
          <input type="range" className="min-w-0 flex-1 accent-primary" disabled={!canSeek}
            min={0} max={Math.max(0, state.duration - 0.1)} step={0.1} value={seek ?? state.liveTime}
            onChange={event => setSeek(event.currentTarget.valueAsNumber)}
            onBlur={() => setSeek(null)}
            onPointerUp={event => { void command('live.seek', { value: event.currentTarget.valueAsNumber }); setSeek(null) }}
            onKeyUp={event => { if (seek !== null && (event.key.startsWith('Arrow') || event.key === 'Home' || event.key === 'End')) { void command('live.seek', { value: seek }); setSeek(null) } }} />
        </label>
        <label className="flex items-center gap-2 text-xs">
          <input type="checkbox" checked={state.aspect} onChange={event => void command('live.aspect', { value: event.currentTarget.checked })} />
          接管时使用 16:9（测试）
        </label>
        <label className="flex items-center gap-2 text-xs">
          <input type="checkbox" checked={state.loop} disabled={!canSeek}
            onChange={event => void command('live.loop', { value: event.currentTarget.checked })} />
          结束前循环
        </label>
        {state.loop && <label className="flex items-center gap-2 text-xs">
          提前（秒）
          <select className="h-8 rounded-md border bg-background px-2" value={state.loopLead} disabled={!canSeek}
            onChange={event => void command('live.loopLead', { value: Number(event.currentTarget.value) })}>
            {[...new Set([0.5, 1, 1.5, 2, 2.5, 3, state.loopLead])].sort((a, b) => a - b).map(value =>
              <option key={value} value={value}>{Number(value.toFixed(2))}</option>)}
          </select>
        </label>}
      </div>
      <details className="rounded-md border p-3">
        <summary className="cursor-pointer text-xs font-medium">诊断与测试</summary>
        <div className="mt-3 flex flex-wrap items-center gap-2">
          <Button size="sm" variant="outline" disabled={!state.viewing} onClick={() => void command('live.record')}>记录 Live 状态</Button>
          <Button size="sm" variant="outline" onClick={() => void command('live.swingReport')}>读取上次 Swing 报告</Button>
          <Button size="sm" variant="outline" disabled={!state.liveClock && (!state.liveClockAvailable || !state.paused || state.active)}
            onClick={() => void command('live.clock', { value: !state.liveClock })}>
            {state.liveClock ? '停止角色图实验并恢复' : '角色图运行 3 秒'}
          </Button>
          <label className="flex items-center gap-2 text-xs"><input type="checkbox" checked={state.liveDiagnostics}
            onChange={event => void command('live.armDiagnostics', { value: event.currentTarget.checked })} />连续记录手臂与服装骨</label>
        </div>
        {state.liveClockNotice && <p className="mt-2 text-xs text-muted-foreground">{state.liveClockNotice}</p>}
        <p className="mt-2 text-xs text-muted-foreground">
          {state.liveFps > 0 ? `Unity 更新 ${state.liveFps.toFixed(1)} FPS · 本次记录卡顿 ${state.liveStalls} 次` : '等待 Live 帧采样'}
          {state.swingReportLoaded && ' · 已读取 Swing 报告'}
        </p>
      </details>
    </section>
  )
}

export function ForkPlacementFields({ slot, token, generation, initial, yaw, command, labelPrefix }: {
  slot: number; token: number; generation: number; initial: Vector3; yaw: number; command: ForkCommand; labelPrefix?: string
}) {
  const apply = (patch: { position?: Vector3; yaw?: number }) =>
    void command('formation.place', { slot, actorToken: token, generation, ...patch })
  return (
    <Vector3Field labelPrefix={labelPrefix ?? (slot < 0 ? '主角色站位' : `副角色 ${slot + 1} 站位`)}
      value={initial} min={-256} max={256} step={0.01} disabled={!token}
      onCommit={position => apply({ position })}
      heading={{ value: yaw, onCommit: value => apply({ yaw: value }) }} />
  )
}

function Follower({ slot, follower, command }: { slot: number; follower?: ForkFollower; command: ForkCommand }) {
  const actors = useMotionStore(state => state.actors.actors)
  const entries = useMotionStore(state => state.library.entries)
  const playback = useMotionStore(state => state.player.state)
  const bindingLocked = playback === 'playing' || playback === 'paused'
  const [actor, setActor] = useState(follower?.actorToken ?? 0)
  const [key, setKey] = useState(follower?.motionKey ?? '')
  const [offset, setOffset] = useState(follower?.offsetSeconds ?? 0)
  const boundActor = actors.find(item => item.token === follower?.actorToken)
  return (
    <section className="min-w-0 space-y-2 rounded-lg border bg-background/70 p-3">
      <div className="flex items-center justify-between gap-2">
        <h3 className="text-sm font-medium">副角色 {slot + 1}</h3>
        <span className="text-xs text-muted-foreground">{follower?.physics || '尚未绑定'}</span>
      </div>
      <div className="mt-3 flex flex-wrap items-end gap-2">
        <label className="min-w-32 text-xs text-muted-foreground">角色
          <select className="mt-1 block h-8 w-full rounded-md border bg-background px-2 text-foreground" value={actor} onChange={event => setActor(Number(event.currentTarget.value))}>
            <option value={0}>选择角色</option>
            {actors.filter(item => !item.selected).map(item => <option key={item.token} value={item.token}>{item.label}</option>)}
          </select>
        </label>
        <label className="min-w-36 flex-1 text-xs text-muted-foreground">动作
          <select className="mt-1 block h-8 w-full rounded-md border bg-background px-2 text-foreground" value={key} onChange={event => setKey(event.currentTarget.value)}>
            <option value="">选择曲库动作</option>
            {entries.map(item => <option key={item.id} value={item.id}>{item.title}</option>)}
          </select>
        </label>
        <label className="w-20 text-xs text-muted-foreground">偏移 秒
          <Input className="mt-1 h-8 text-foreground" type="number" step={0.05} value={Number.isFinite(offset) ? offset : ''}
            onChange={event => setOffset(event.currentTarget.valueAsNumber)} />
        </label>
        <Button size="sm" disabled={bindingLocked || !actor || !key || !Number.isFinite(offset)}
          onClick={() => void command('formation.bind', { slot, actorToken: actor, motionKey: key, offsetSeconds: offset })}>绑定</Button>
        {Boolean(follower?.actorToken) && <Button size="sm" variant="ghost" disabled={bindingLocked}
          onClick={() => void command('formation.clear', { slot })}>解除</Button>}
      </div>
      {follower?.generation ? <ForkPlacementFields slot={slot} token={follower.actorToken} generation={follower.generation}
        initial={follower.position} yaw={follower.yaw} command={command} /> : null}
      {follower?.generation ? <Button size="sm" variant="outline" onClick={() => void command('formation.place', {
        slot, actorToken: follower.actorToken, generation: follower.generation, position: zero, yaw: 0,
      })}>重置站位与朝向</Button> : null}
      {boundActor && <ModelVisibility actor={boundActor} command={command} />}
    </section>
  )
}

function ModelVisibility({ actor, command }: { actor: { token: number; modelHidden?: boolean }; command: ForkCommand }) {
  if (actor.modelHidden === undefined) return null
  return <label className="flex items-center gap-2 text-xs text-muted-foreground">
    <input type="checkbox" checked={actor.modelHidden}
      onChange={event => void command('actor.hideModel', { actorToken: actor.token, value: event.currentTarget.checked })} />
    隐藏角色模型
  </label>
}

export function ForkEnsembleControls({ state, command }: { state: ForkState; command: ForkCommand }) {
  const primary = useMotionStore(model => model.actors.actors.find(actor => actor.selected))
  const playback = useMotionStore(model => model.player.state)
  const bindingLocked = playback === 'playing' || playback === 'paused'
  const [draftSlotCount, setDraftSlotCount] = useState(0)
  const slots = Math.max(state.followers.length, draftSlotCount)
  return (
    <div className="space-y-3">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <span className="text-xs text-muted-foreground">主角色提供播放时钟和镜头</span>
        <div className="flex flex-wrap gap-2">
          <Button size="sm" variant="outline" disabled={bindingLocked} onClick={() => void command('formation.auto')}>按名称自动分配</Button>
          <Button size="sm" variant="outline" onClick={() => void command('formation.align')}>对齐原曲队形</Button>
          <Button size="sm" disabled={slots >= 64} onClick={() => setDraftSlotCount(slots + 1)}>添加副角色</Button>
        </div>
      </div>
      {bindingLocked && <p className="text-xs text-muted-foreground">停止播放后可新增或解除动作绑定；当前仍可调整已绑定角色的站位。</p>}
      <div className="grid gap-3 md:grid-cols-2 2xl:grid-cols-3">
      <section aria-label="主角色" className="min-w-0 space-y-2 rounded-lg border border-primary/40 bg-background/70 p-3">
        <div className="flex flex-wrap items-center justify-between gap-2">
          <h3 className="text-sm font-medium">主角色{primary ? ` · ${primary.label}` : ''}</h3>
          <span className="text-xs text-muted-foreground">物理：{state.primaryPhysics || '暂无当前数据'}</span>
        </div>
        <dl className="grid grid-cols-2 gap-2 text-xs sm:grid-cols-4">
          {(['x', 'y', 'z'] as const).map(axis => (
            <div className="flex items-baseline gap-2" key={axis}>
              <dt className="text-muted-foreground">{axis.toUpperCase()}</dt>
              <dd className="tabular-nums">{state.position[axis].toFixed(2)}</dd>
            </div>
          ))}
          <div className="flex items-baseline gap-2">
            <dt className="text-muted-foreground">朝向</dt>
            <dd className="tabular-nums">{state.yaw.toFixed(1)}°</dd>
          </div>
        </dl>
        {primary && <ModelVisibility actor={primary} command={command} />}
      </section>
      {Array.from({ length: slots }, (_, slot) => {
        const follower = state.followers[slot]
        return <Follower key={`${state.epoch}:${slot}:${follower?.generation ?? 0}`} slot={slot} follower={follower} command={command} />
      })}
      </div>
      {state.formationNotice && <p className="text-xs text-muted-foreground">{state.formationNotice}</p>}
    </div>
  )
}
