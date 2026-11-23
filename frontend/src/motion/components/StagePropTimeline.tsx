import { useEffect, useRef, useState, type PointerEvent } from 'react'
import type { StagePropCue, Vector3 } from '../protocol'
import type { ForkCommand } from './ForkWorkspace'
import { Vector3Field } from './NumberField'

const props = ['黄白狗', '黑白狗', '1205-02', '1205-03', '白兔', '棕兔', '粉兔', '斑点狗', '1548-01', '可莉饼 A', '可莉饼 B']
const anchors = ['角色骨骼', '镜头前', '舞台坐标', '隐藏', '镜头交接 → 舞台固定']
const movements = ['固定 / 瞬切', '直线移动', '缓入缓出', '抛物线', '角色交接']
const bones = [[-1, '自动握持'], [17, '左手'], [18, '右手'], [0, '髋部'], [7, '脊柱'], [8, '胸部'], [10, '头部'], [27, '左手手指'], [42, '右手手指']] as const
const duration = 4806
const zero = { x: 0, y: 0, z: 0 }
const seconds = (frame: number) => (frame / 30).toFixed(2)
const clamp = (frame: number, lo = 0, hi = duration) => Math.max(lo, Math.min(hi, frame))
const button = 'rounded border border-border px-2.5 py-1 text-xs hover:bg-muted disabled:opacity-40'
const field = 'rounded border border-border bg-background px-2 py-1 text-sm'
type Draft = { index: number; cue: StagePropCue }
type Drag = { draft: Draft; start: number; width: number; edge: 'move' | 'first' | 'last'; moved: boolean }
const normalize = (cue: StagePropCue) => ({
  anchor: 0, movement: 0, bone: -1, blendFrames: 10, endPosition: cue.position, endRotation: cue.rotation,
  arcHeight: 0.5, hideAfter: false, ...cue,
})

export function StagePropTimeline({ cues, time, samples, command }: {
  cues: StagePropCue[]
  time: number
  samples?: ({ frame: number; position: Vector3 } | null)[]
  command: ForkCommand
}) {
  const [draft, setDraft] = useState<Draft>()
  const [prop, setProp] = useState(1)
  const [zoom, setZoom] = useState(2)
  const [confirmReset, setConfirmReset] = useState(false)
  const [saving, setSaving] = useState(false)
  const [scrubFrame, setScrubFrame] = useState<number>()
  const [pendingFrame, setPendingFrame] = useState<number>()
  const drag = useRef<Drag | undefined>(undefined)
  const viewport = useRef<HTMLDivElement>(null)
  const surface = useRef<HTMLDivElement>(null)
  const commandRef = useRef(command)
  commandRef.current = command
  const frame = clamp(scrubFrame ?? pendingFrame ?? Math.round(time * 30))
  const cue = draft && normalize(draft.cue)
  const sample = cue && samples?.[cue.prop - 1]
  const sampleReady = sample && Math.abs(sample.frame - frame) <= 1
  const positionLimit = cue?.anchor === 0 ? 2 : 200
  const invalid = cue && (cue.first < 0 || cue.first >= cue.last || cue.last > duration)
    ? '结束帧必须晚于开始帧。'
    : cue && cues.some((other, index) => index !== draft?.index && other.prop === cue.prop &&
        other.first < cue.last && cue.first < other.last)
      ? '同一道具的片段不能重叠。' : ''
  const valid = Boolean(cue && !invalid)
  useEffect(() => () => { void commandRef.current('externalStage.cuePreviewClear', {}, true) }, [])
  useEffect(() => {
    if (!draft || saving) return
    if (!valid) {
      void commandRef.current('externalStage.cuePreviewClear', {}, true)
      return
    }
    const timer = window.setTimeout(() => {
      void commandRef.current('externalStage.cuePreview', { index: draft.index, ...normalize(draft.cue) }, true)
    }, 55)
    return () => window.clearTimeout(timer)
  }, [draft, valid, saving])

  useEffect(() => {
    if (pendingFrame === undefined) return
    const timer = window.setTimeout(() => setPendingFrame(undefined), 1000)
    return () => window.clearTimeout(timer)
  }, [pendingFrame])
  useEffect(() => {
    if (pendingFrame !== undefined && Math.round(time * 30) === pendingFrame) setPendingFrame(undefined)
  }, [pendingFrame, time])

  function seek(target: number) {
    setPendingFrame(clamp(target))
    void commandRef.current('externalStage.cueSeek', { frame: clamp(target) }, true)
  }

  function close() {
    setDraft(undefined)
    void commandRef.current('externalStage.cuePreviewClear', {}, true)
  }

  function update(change: Partial<StagePropCue>) {
    setDraft((current) => current && { ...current, cue: { ...current.cue, ...change } })
  }

  function create(anchor = 0, movement = 0) {
    const first = clamp(frame, 0, duration - 1)
    const next = cues.filter((item) => item.prop === prop && item.first > first)
      .reduce((end, item) => Math.min(end, item.first), duration)
    const start = anchor === 1 ? { x: -0.4, y: -0.45, z: 0.86 } : zero
    setDraft({ index: cues.length, cue: normalize({
      prop, place: 1, left: false, first, last: Math.min(next, first + 30),
      position: start, rotation: zero, endPosition: start, anchor, movement, hideAfter: true,
    }) })
  }

  async function apply(action: string, fields: Record<string, unknown> = {}) {
    setSaving(true)
    const ok = await commandRef.current(action, fields)
    setSaving(false)
    if (ok !== false) close()
  }

  function pointerFrame(event: PointerEvent) {
    const bounds = surface.current?.getBoundingClientRect()
    return bounds && bounds.width ? clamp(Math.round((event.clientX - bounds.left) / bounds.width * duration)) : frame
  }

  function startDrag(event: PointerEvent<HTMLButtonElement>, edit: Draft, edge: Drag['edge']) {
    if (event.button !== 0) return
    event.currentTarget.setPointerCapture(event.pointerId)
    drag.current = { draft: edit, start: event.clientX, width: surface.current?.getBoundingClientRect().width || 1, edge, moved: false }
    setProp(edit.cue.prop)
    setDraft(edit)
  }

  function moveDrag(event: PointerEvent<HTMLButtonElement>) {
    const active = drag.current
    if (!active) return
    const diff = event.clientX - active.start
    if (!active.moved && Math.abs(diff) < 3) return
    active.moved = true
    const delta = Math.round(diff / active.width * duration)
    const original = active.draft.cue
    const length = original.last - original.first
    const first = active.edge === 'last' ? original.first : clamp(original.first + delta, 0,
      active.edge === 'first' ? original.last - 1 : duration - length)
    const last = active.edge === 'first' ? original.last : active.edge === 'move' ? first + length :
      clamp(original.last + delta, original.first + 1)
    setDraft({ ...active.draft, cue: { ...original, first, last } })
  }

  function endDrag() {
    const active = drag.current
    drag.current = undefined
    if (active && !active.moved) seek(active.edge === 'last' ? active.draft.cue.last - 1 : active.draft.cue.first)
  }

  function capturePosition(endpoint: 'position' | 'endPosition') {
    if (!sampleReady || !cue) return
    update({ anchor: 2, [endpoint]: sample.position, ...(cue.anchor !== 2 ? {
      position: sample.position, endPosition: sample.position, movement: cue.movement === 4 ? 0 : cue.movement,
    } : {}) })
  }

  const clips = cues.map((item, index) => ({ index, cue: draft?.index === index ? draft.cue : item }))
  if (draft && draft.index >= cues.length) clips.push(draft)
  const clockStep = zoom >= 8 ? 150 : zoom >= 4 ? 300 : 600
  const savedCue = draft && cues[draft.index] && normalize(cues[draft.index])
  const canSplit = savedCue && savedCue.movement === 0 && savedCue.anchor !== 4 &&
    frame > savedCue.first && frame < savedCue.last
  const canLeaveOnStage = savedCue && savedCue.anchor === 1 && savedCue.movement === 0 && cue &&
    frame > savedCue.first && frame <= savedCue.last && frame < duration &&
    JSON.stringify(cue) === JSON.stringify(savedCue) &&
    !cues.some((other) => other.prop === savedCue.prop && other.first === frame)
  const moving = cue && cue.movement >= 1 && cue.movement <= 3

  return (
    <section className="min-w-0 space-y-3 rounded-lg border border-border bg-card p-3" aria-label="道具时间轴">
      <div className="flex flex-wrap items-center gap-2">
        <h2 className="mr-2 text-sm font-semibold">道具时间轴</h2>
        <time className="rounded bg-muted px-2 py-1 font-mono text-xs tabular-nums">{seconds(frame)} 秒 · {frame}f</time>
        <button type="button" className={button} onClick={() => seek(frame - 1)}>上一帧</button>
        <button type="button" className={button} onClick={() => seek(frame + 1)}>下一帧</button>
        <label className="ml-auto flex items-center gap-2 text-xs">缩放
          <select className={field} value={zoom} onChange={(event) => setZoom(Number(event.target.value))}>
            {[1, 2, 4, 8, 16].map((value) => <option key={value} value={value}>{value}×</option>)}
          </select>
        </label>
        <button type="button" className={button} onClick={() => {
          if (viewport.current && surface.current)
            viewport.current.scrollLeft = surface.current.offsetWidth * frame / duration - viewport.current.clientWidth / 2
        }}>定位播放头</button>
      </div>
      <div className="overflow-x-auto rounded-md border border-border" ref={viewport}>
        <div className="relative grid min-w-full grid-cols-[92px_1fr]" style={{ width: Math.max(540, zoom * 420) }}>
          <div className="sticky left-0 z-20 border-r border-border bg-card">
            <div className="flex h-8 items-center border-b px-2 text-xs font-medium">道具轨道</div>
            {props.map((name, index) => (
              <button type="button" key={name} aria-pressed={prop === index + 1}
                className={'block h-8 w-full border-b px-2 text-left text-xs ' + (prop === index + 1 ? 'bg-primary/20 font-semibold' : 'hover:bg-muted')}
                onClick={() => { setProp(index + 1); close() }}>{name}</button>
            ))}
          </div>
          <div className="relative min-w-0" ref={surface}>
            <div className="relative h-8 cursor-ew-resize touch-none border-b bg-muted/60" aria-label="拖动播放头"
              onPointerDown={(event) => {
                if (event.button !== 0) return
                event.currentTarget.setPointerCapture(event.pointerId)
                setScrubFrame(pointerFrame(event))
              }}
              onPointerMove={(event) => {
                if (event.currentTarget.hasPointerCapture(event.pointerId)) setScrubFrame(pointerFrame(event))
              }}
              onPointerUp={(event) => { seek(pointerFrame(event)); setScrubFrame(undefined) }}
              onPointerCancel={() => setScrubFrame(undefined)}>
              {Array.from({ length: Math.floor(duration / clockStep) + 1 }, (_, index) => (
                <span key={index} className="pointer-events-none absolute top-1 h-7 border-l border-border pl-1 font-mono text-[10px]"
                  style={{ left: 100 * index * clockStep / duration + '%' }}>{index * clockStep / 30}s</span>
              ))}
            </div>
            {props.map((name, slot) => (
              <div key={name} className={'relative h-8 border-b ' + (prop === slot + 1 ? 'bg-primary/5' : 'bg-muted/15')}>
                {clips.filter((item) => item.cue.prop === slot + 1).map((item) => {
                  const model = normalize(item.cue)
                  const selected = draft?.index === item.index
                  const handlers = (edge: Drag['edge']) => ({
                    onPointerDown: (event: PointerEvent<HTMLButtonElement>) => startDrag(event, item, edge),
                    onPointerMove: moveDrag,
                    onPointerUp: endDrag,
                    onPointerCancel: () => { if (drag.current) setDraft(drag.current.draft); drag.current = undefined },
                  })
                  return <div key={item.index} className={'absolute top-1 flex h-6 min-w-[9px] overflow-hidden rounded border ' +
                    (selected ? 'z-10 border-primary bg-primary text-primary-foreground' : 'border-primary/30 bg-primary/20')}
                    style={{ left: model.first / duration * 100 + '%', width: Math.max(0, model.last - model.first) / duration * 100 + '%' }}>
                    {selected && <button type="button" aria-label="拖动开始边缘" className="w-2 shrink-0 cursor-ew-resize touch-none bg-black/20" {...handlers('first')} />}
                    <button type="button" className="min-w-0 flex-1 cursor-grab truncate px-1 text-left text-[10px] touch-none"
                      title={name + ' · ' + anchors[model.anchor] + ' · ' + seconds(model.first) + '–' + seconds(model.last) + ' 秒'}
                      onClick={(event) => { if (event.detail === 0) { setProp(model.prop); setDraft(item); seek(model.first) } }}
                      {...handlers('move')}>{anchors[model.anchor]}{model.movement > 0 ? ' · ' + movements[model.movement] : ''}</button>
                    {selected && <button type="button" aria-label="拖动结束边缘" className="w-2 shrink-0 cursor-ew-resize touch-none bg-black/20" {...handlers('last')} />}
                  </div>
                })}
              </div>
            ))}
            <span aria-hidden className="pointer-events-none absolute top-0 z-10 h-full w-px bg-red-500" style={{ left: frame / duration * 100 + '%' }} />
          </div>
        </div>
      </div>

      <div className="flex flex-wrap items-center gap-2">
        <button type="button" className={button} onClick={() => create()}>在当前帧新增</button>
        <button type="button" className={button} onClick={() => create(1)}>新增镜头固定</button>
        <button type="button" className={button} onClick={() => create(3)}>新增隐藏</button>
        {!confirmReset
          ? <button type="button" className={button} onClick={() => setConfirmReset(true)}>恢复舞台预设</button>
          : <><button type="button" className={button} disabled={saving} onClick={async () => {
            await apply('externalStage.cueReset'); setConfirmReset(false)
          }}>确认恢复全部片段</button>
          <button type="button" className={button} onClick={() => setConfirmReset(false)}>取消恢复</button></>}
      </div>

      {cue && draft ? (
        <div className="space-y-3 rounded-md border border-border p-3">
          <div className="flex flex-wrap items-center gap-2">
            <h3 className="mr-auto text-sm font-semibold">{props[cue.prop - 1]} · 片段属性</h3>
            <button type="button" className={button} onClick={() => seek(cue.first)}>查看开始</button>
            <button type="button" className={button} onClick={() => seek(cue.last - 1)}>查看末帧</button>
            <button type="button" className={button} onClick={() => seek(cue.last)}>查看结束后</button>
          </div>
          <div className="flex flex-wrap items-end gap-3 text-xs">
            <label className="grid gap-1">固定位置
              <select className={field} value={cue.anchor} onChange={(event) => {
                const anchor = Number(event.target.value)
                const position = anchor === 1 || anchor === 4 ? { x: -0.4, y: -0.45, z: 0.86 } : zero
                if ((cue.anchor === 1 || cue.anchor === 4) && (anchor === 1 || anchor === 4)) {
                  update({ anchor, movement: 0 })
                } else {
                  update({ anchor, movement: 0, position, endPosition: position, rotation: zero, endRotation: zero })
                }
              }}>{anchors.map((label, index) => <option key={label} value={index}>{label}</option>)}</select>
            </label>
            {cue.anchor !== 3 && cue.anchor !== 4 && <label className="grid gap-1">移动方式
              <select className={field} value={cue.movement} onChange={(event) => {
                const movement = Number(event.target.value)
                const start = sampleReady ? sample.position : zero
                update(movement === 3 && cue.anchor !== 2 ?
                  { anchor: 2, movement, position: start, endPosition: start, endRotation: cue.rotation } :
                  { movement, ...(cue.movement === 0 && movement !== 4 ?
                    { endPosition: cue.position, endRotation: cue.rotation } : {}) })
              }}>
                {movements.map((label, index) => (index !== 4 || cue.anchor === 0) && <option key={label} value={index}>{label}</option>)}
              </select>
            </label>}
            {cue.anchor === 0 && <>
              <label className="grid gap-1">角色站位
                <select className={field} value={cue.place} onChange={(event) => update({ place: Number(event.target.value) })}>
                  {Array.from({ length: 10 }, (_, index) => <option key={index} value={index + 1}>{index + 1} 号位</option>)}
                </select>
              </label>
              <label className="grid gap-1">绑定骨骼
                <select className={field} value={cue.bone} onChange={(event) => update({ bone: Number(event.target.value) })}>
                  {bones.map(([id, name]) => <option key={id} value={id}>{name}</option>)}
                  {Array.from({ length: 55 }, (_, id) => id).filter((id) => !bones.some(([known]) => known === id))
                    .map((id) => <option key={id} value={id}>人形骨骼 {id}</option>)}
                </select>
              </label>
              {cue.bone === -1 && <label className="grid gap-1">持物手
                <select className={field} value={cue.left ? 'left' : 'right'} onChange={(event) => update({ left: event.target.value === 'left' })}>
                  <option value="right">右手</option><option value="left">左手</option>
                </select>
              </label>}
            </>}
            {(['first', 'last'] as const).map((key) => <label key={key} className="grid gap-1">
              {key === 'first' ? '开始帧' : '结束帧（不含）'}
              <span className="flex gap-1"><input className={field + ' w-20'} type="number" min={0} max={duration} value={cue[key]}
                onChange={(event) => update({ [key]: Number(event.target.value) })} />
                <button type="button" className={button} onClick={() => update({ [key]: frame })}>当前帧</button></span>
            </label>)}
            {cue.movement === 4 && <label className="grid gap-1">交接帧数
              <input className={field + ' w-20'} type="number" min={1} max={duration} value={cue.blendFrames}
                onChange={(event) => update({ blendFrames: clamp(Number(event.target.value), 1) })} />
            </label>}
            {cue.movement === 3 && <label className="grid gap-1">抛物线高度（米）
              <input className={field + ' w-20'} type="number" min={0} max={20} step={0.05} value={cue.arcHeight}
                onChange={(event) => update({ arcHeight: clamp(Number(event.target.value), 0, 20) })} />
            </label>}
          </div>
          {cue.movement === 4 && !cues.some((prior) => prior.prop === cue.prop && prior.last === cue.first && (prior.anchor ?? 0) === 0) &&
            <p className="text-xs text-muted-foreground">交接需要紧接同一道具的角色绑定片段。</p>}
          {cue.anchor === 4 && <p className="text-xs text-muted-foreground">
            按开始帧的曲目镜头计算落位，后续留在舞台原处。使用曲目镜头并关闭角色追随。
          </p>}
          {cue.anchor !== 3 && <>
            <div className="grid gap-2 lg:grid-cols-2">
              {(['position', 'rotation', ...(moving ? ['endPosition', 'endRotation'] as const : [])] as const).map((key) => {
                const position = key === 'position' || key === 'endPosition'
                const label = (key.startsWith('end') ? '终点' : '起点') + (position ? '位置' : '旋转')
                return <fieldset key={key} className="rounded border border-border p-2">
                  <legend className="px-1 text-xs">{label}{position ? cue.anchor === 4 ? '（交接时的画面 X/Y、距离 Z）' : cue.anchor === 1 ? '（画面 X/Y、距离 Z）' : '（米）' : '（度）'}</legend>
                  <Vector3Field value={cue[key]} labelPrefix={label} min={position ? -positionLimit : -360} max={position ? positionLimit : 360}
                    step={position ? 0.01 : 1} dragSpeed={position ? 0.005 : 0.5} decimals={position ? 2 : 0}
                    onChange={(next) => update({ [key]: next })} onCommit={(next) => update({ [key]: next })} />
                </fieldset>
              })}
            </div>
            <div className="flex flex-wrap items-center gap-2 text-xs">
              <button type="button" className={button} disabled={!sampleReady} onClick={() => capturePosition('position')}>使用当前道具位置作为起点</button>
              {moving && <button type="button" className={button} disabled={!sampleReady || cue.anchor !== 2}
                onClick={() => capturePosition('endPosition')}>取当前位置为终点</button>}
              <label className="flex items-center gap-1"><input type="checkbox" checked={cue.hideAfter}
                onChange={(event) => update({ hideAfter: event.target.checked })} />片段结束后隐藏</label>
            </div>
          </>}
          {invalid && <p role="alert" className="text-xs text-destructive">{invalid}</p>}
          <div className="flex flex-wrap gap-2">
            <button type="button" className={button + ' border-primary bg-primary text-primary-foreground'} disabled={!valid || saving}
              onClick={() => void apply('externalStage.cueSave', { index: draft.index, ...cue })}>保存片段</button>
            <button type="button" className={button} disabled={saving || !canSplit}
              onClick={() => void apply('externalStage.cueSplit', { index: draft.index, frame })}>在当前帧分割已保存片段</button>
            {savedCue && savedCue.anchor === 1 && <button type="button" className={button} disabled={saving || !canLeaveOnStage}
              title="保存镜头固定片段后，选择片段内或片段结束的交接帧。舞台片段结束时隐藏。"
              onClick={() => void apply('externalStage.cueLeaveOnStage', { index: draft.index, frame })}>在当前帧留在舞台</button>}
            <button type="button" className={button} disabled={saving || draft.index >= cues.length}
              onClick={() => void apply('externalStage.cueDelete', { index: draft.index })}>删除片段</button>
            <button type="button" className={button} disabled={saving} onClick={close}>取消预览</button>
          </div>
        </div>
      ) : <p className="text-xs text-muted-foreground">点片段定位，拖动片段或两端修正时机。修改会实时预览，开启「跟随曲目」后使用已保存轨道。</p>}
    </section>
  )
}
