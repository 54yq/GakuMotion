import { useEffect, useState } from 'react'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { getMotionBridge } from '../bridge'
import { useMotionStore } from '../store'
import type { ForkFollower, ForkState, Vector3 } from '../protocol'

const zero = { x: 0, y: 0, z: 0 }
const mockState: ForkState = {
  session: 1, owner: 1, epoch: 1, revision: 1, observed: true, viewing: true,
  paused: true, active: true, available: true, aspect: true, loop: false, loopLead: 1, liveTime: 42, duration: 155,
  liveNotice: 'Live 已暂停，可以为场景角色选择动作。',
  position: zero, yaw: 0, formationNotice: '', presetNotice: '', presets: ['三人舞台'],
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
    </section>
  )
}

function Placement({ slot, token, generation, initial, yaw, command }: {
  slot: number; token: number; generation: number; initial: Vector3; yaw: number; command: ForkCommand
}) {
  const [draft, setDraft] = useState({ ...initial, yaw })
  const [dirty, setDirty] = useState(false)
  const values = dirty ? draft : { ...initial, yaw }
  return (
    <div className="mt-3 flex flex-wrap items-end gap-2">
      {(['x', 'y', 'z', 'yaw'] as const).map(key => (
        <label className="w-20 text-xs text-muted-foreground" key={key}>
          {key === 'yaw' ? '朝向 °' : `${key.toUpperCase()} 米`}
          <Input className="mt-1 h-8 text-foreground" type="number" step={key === 'yaw' ? 1 : 0.05}
            value={Number.isFinite(values[key]) ? values[key] : ''}
            onChange={event => { setDraft({ ...values, [key]: event.currentTarget.valueAsNumber }); setDirty(true) }} />
        </label>
      ))}
      <Button size="sm" variant="secondary" disabled={!token || !Object.values(values).every(Number.isFinite)}
        onClick={async () => { await command('formation.place', { slot, actorToken: token, generation,
          position: { x: values.x, y: values.y, z: values.z }, yaw: values.yaw }); setDirty(false) }}>应用站位</Button>
    </div>
  )
}

function Follower({ slot, follower, command }: { slot: number; follower?: ForkFollower; command: ForkCommand }) {
  const actors = useMotionStore(state => state.actors.actors)
  const entries = useMotionStore(state => state.library.entries)
  const [actor, setActor] = useState(follower?.actorToken ?? 0)
  const [key, setKey] = useState(follower?.motionKey ?? '')
  const [offset, setOffset] = useState(follower?.offsetSeconds ?? 0)
  return (
    <section className="rounded-lg border bg-card p-4">
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
        <Button size="sm" disabled={!actor || !key || !Number.isFinite(offset)}
          onClick={() => void command('formation.bind', { slot, actorToken: actor, motionKey: key, offsetSeconds: offset })}>绑定</Button>
        {Boolean(follower?.actorToken) && <Button size="sm" variant="ghost" onClick={() => void command('formation.clear', { slot })}>解除</Button>}
      </div>
      {follower?.generation ? <Placement slot={slot} token={follower.actorToken} generation={follower.generation}
        initial={follower.position} yaw={follower.yaw} command={command} /> : null}
    </section>
  )
}

export function FormationControls({ state, command }: { state: ForkState; command: ForkCommand }) {
  const primary = useMotionStore(model => model.actors.actors.find(actor => actor.selected))
  const playing = useMotionStore(model => model.player.state === 'playing')
  const [extraSlot, setExtraSlot] = useState(false)
  const [preset, setPreset] = useState('')
  const slots = state.followers.length + Number(extraSlot)
  useEffect(() => { setExtraSlot(false) }, [state.followers.length, state.epoch])
  return (
    <div className="space-y-3">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <h2 className="text-lg font-semibold">多人站位</h2>
        <div className="flex flex-wrap gap-2">
          <Button size="sm" variant="outline" disabled={playing} onClick={() => void command('formation.auto')}>按名称自动分配</Button>
          <Button size="sm" variant="outline" onClick={() => void command('formation.align')}>对齐原曲队形</Button>
          <Button size="sm" disabled={extraSlot || slots >= 64} onClick={() => setExtraSlot(true)}>添加副角色</Button>
        </div>
      </div>
      <section className="rounded-lg border bg-card p-4">
        <div className="flex items-center justify-between gap-2">
          <h3 className="text-sm font-medium">{primary?.label ?? '主角色'}</h3>
          <Button size="sm" variant="ghost" disabled={!primary} onClick={() => void command('formation.savePrimary')}>保存主角站位</Button>
        </div>
        <Placement key={primary?.token} slot={-1} token={primary?.token ?? 0} generation={0}
          initial={state.position} yaw={state.yaw} command={command} />
      </section>
      {Array.from({ length: slots }, (_, slot) => {
        const follower = state.followers[slot]
        return <Follower key={`${state.epoch}:${slot}:${follower?.generation ?? 0}`} slot={slot} follower={follower} command={command} />
      })}
      <div className="flex flex-wrap items-end gap-2 rounded-lg border bg-card p-4">
        <label className="text-xs text-muted-foreground">队形预设
          <Input className="mt-1 h-8 text-foreground" list="formation-presets" value={preset} onChange={event => setPreset(event.currentTarget.value)} />
          <datalist id="formation-presets">{state.presets.map(name => <option key={name} value={name} />)}</datalist>
        </label>
        <Button size="sm" variant="secondary" disabled={!preset.trim()} onClick={() => void command('formation.save', { name: preset.trim() })}>保存</Button>
        <Button size="sm" variant="secondary" disabled={!state.presets.includes(preset)} onClick={() => void command('formation.load', { name: preset })}>载入</Button>
        {(state.presetNotice || state.formationNotice) && <p className="w-full text-xs text-muted-foreground">{state.presetNotice || state.formationNotice}</p>}
      </div>
    </div>
  )
}
