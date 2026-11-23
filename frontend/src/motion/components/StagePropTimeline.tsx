import { useEffect, useRef, useState } from 'react'
import type { StagePropCue } from '../protocol'
import type { ForkCommand } from './ForkWorkspace'
import { Vector3Field } from './NumberField'

const props = ['黄白狗', '黑白狗', '1205-02', '1205-03', '白兔', '棕兔', '粉兔', '斑点狗', '1548-01', '可莉饼 A', '可莉饼 B']
const duration = 4806
const seconds = (frame: number) => (frame / 30).toFixed(2)

export function StagePropTimeline({ cues, time, command }: {
  cues: StagePropCue[]
  time: number
  command: ForkCommand
}) {
  const [draft, setDraft] = useState<{ index: number; cue: StagePropCue }>()
  const [prop, setProp] = useState(1)
  const [confirmReset, setConfirmReset] = useState(false)
  const commandRef = useRef(command)
  commandRef.current = command
  useEffect(() => () => { void commandRef.current('externalStage.cuePreviewClear', {}, true) }, [])
  const frame = Math.round(time * 30)
  const seek = (target: number) => void command('externalStage.cueSeek', { frame: target })
  const closeDraft = () => {
    void command('externalStage.cuePreviewClear', {}, true)
    setDraft(undefined)
  }
  const preview = (index: number, cue: StagePropCue) =>
    void command('externalStage.cuePreview', { index, ...cue }, true)
  const update = (value: Partial<StagePropCue>) => {
    if (!draft) return
    const cue = { ...draft.cue, ...value }
    setDraft({ ...draft, cue })
    if (cue.first >= 0 && cue.last <= duration && cue.first < cue.last) preview(draft.index, cue)
  }
  const create = () => {
    const first = Math.min(frame, duration - 1)
    const cue = {
      prop, place: 1, left: false, first, last: Math.min(first + 30, duration),
      position: { x: 0, y: 0, z: 0 }, rotation: { x: 0, y: 0, z: 0 },
    }
    void command('externalStage.cuePreviewClear', {}, true)
    setDraft({ index: cues.length, cue })
  }

  return (
    <section className="space-y-2 rounded-md border p-3" aria-label="道具编排">
      <div className="flex flex-wrap items-center justify-between gap-2">
        <h2 className="font-medium">道具编排</h2>
        <button type="button" onClick={create}>在当前进度新增片段</button>
        <button type="button" onClick={() => setConfirmReset(true)}>恢复原道具时间轴</button>
      </div>
      {confirmReset && <div className="flex flex-wrap items-center gap-3">
        <span>将已保存的道具时间轴恢复为舞台预设。</span>
        <button type="button" onClick={() => {
          closeDraft()
          setConfirmReset(false)
          void command('externalStage.cueReset')
        }}>确认恢复</button>
        <button type="button" onClick={() => setConfirmReset(false)}>取消</button>
      </div>}
      <p className="text-xs text-muted-foreground">拖动握持位移或旋转可实时预览。开启「跟随曲目」可同步人物动作。时间以 30 帧/秒计。</p>
      <div className="space-y-1">
        {props.map((name, i) => (
          <div key={name} className="flex items-center gap-2">
            <button type="button" aria-pressed={prop === i + 1} className="w-20 shrink-0 text-left text-xs"
              onClick={() => { setProp(i + 1); closeDraft() }}>{name}</button>
            <div className="relative h-5 flex-1 rounded bg-muted">
              {cues.map((cue, index) => cue.prop === i + 1 && (
                <button key={index} type="button"
                  title={seconds(cue.first) + '–' + seconds(cue.last) + ' 秒'}
                  aria-label={name + ' ' + seconds(cue.first) + '–' + seconds(cue.last) + ' 秒'}
                  className={'absolute top-0 h-full rounded ' + (draft?.index === index ? 'bg-primary' : 'bg-primary/60')}
                  style={{ left: (cue.first / duration * 100) + '%', width: (Math.max(0.5, (cue.last - cue.first) / duration * 100)) + '%' }}
                  onClick={() => {
                    setProp(i + 1)
                    setDraft({ index, cue })
                    preview(index, cue)
                    if (frame < cue.first || frame >= cue.last)
                      seek(Math.min(cue.first + (cue.prop === 1 ? 9 : 1), cue.last - 1))
                  }} />
              ))}
              <span aria-hidden className="pointer-events-none absolute top-0 h-full w-px bg-foreground"
                style={{ left: (frame / duration * 100) + '%' }} />
            </div>
          </div>
        ))}
      </div>
      {draft && (
        <div className="space-y-2 border-t pt-2">
          <div className="flex flex-wrap gap-3">
            <label>道具 <select value={draft.cue.prop} onChange={(e) => update({ prop: Number(e.target.value) })}>
              {props.map((name, i) => <option key={name} value={i + 1}>{name}</option>)}
            </select></label>
            <label>站位 <select value={draft.cue.place} onChange={(e) => update({ place: Number(e.target.value) })}>
              {Array.from({ length: 10 }, (_, i) => <option key={i} value={i + 1}>{i + 1} 号位</option>)}
            </select></label>
            <label>持物手 <select value={draft.cue.left ? 'left' : 'right'} onChange={(e) => update({ left: e.target.value === 'left' })}>
              <option value="right">右手</option><option value="left">左手</option>
            </select></label>
          </div>
          <div className="flex flex-wrap items-end gap-3">
            {(['first', 'last'] as const).map((field) => (
              <label key={field} className="text-sm">{field === 'first' ? '拿起帧' : '放下帧'}（{seconds(draft.cue[field])} 秒）
                <input type="number" className="ml-1 w-20" min={0} max={duration} step={1} value={draft.cue[field]}
                  onChange={(e) => update({ [field]: Number(e.target.value) })} />
                <button type="button" className="ml-2" onClick={() => update({ [field]: Math.min(frame, duration) })}>使用当前帧</button>
              </label>
            ))}
          </div>
          {(['position', 'rotation'] as const).map((field) => (
            <fieldset key={field} className="flex flex-wrap gap-2">
              <legend className="text-sm">{field === 'position' ? '握持位移（米）' : '握持旋转（度）'}</legend>
              <Vector3Field value={draft.cue[field]} labelPrefix={field === 'position' ? '握持位移' : '握持旋转'}
                min={field === 'position' ? -2 : -360} max={field === 'position' ? 2 : 360}
                step={field === 'position' ? 0.01 : 1} dragSpeed={field === 'position' ? 0.005 : 0.5}
                decimals={field === 'position' ? 2 : 0}
                onChange={(next) => update({ [field]: next })}
                onCommit={(next) => update({ [field]: next })} />
            </fieldset>
          ))}
          <div className="flex flex-wrap gap-3">
            <button type="button" onClick={() => void command('externalStage.cueSave', { index: draft.index, ...draft.cue })}>保存</button>
            <button type="button" onClick={() => seek(draft.cue.first)}>跳到拿起帧</button>
            <button type="button" onClick={() => seek(draft.cue.last)}>跳到放下帧</button>
            {draft.index < cues.length && <button type="button" onClick={() => {
              void command('externalStage.cueDelete', { index: draft.index })
              closeDraft()
            }}>删除片段</button>}
            <button type="button" onClick={closeDraft}>关闭编辑</button>
          </div>
        </div>
      )}
    </section>
  )
}
