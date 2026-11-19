import { RiRefreshFill, RiSearchFill } from '@remixicon/react'
import { useState } from 'react'
import { Button } from '@/components/ui/button'
import { Checkbox } from '@/components/ui/checkbox'
import { Input } from '@/components/ui/input'
import { NumberField } from './NumberField'
import { CollapsiblePropertySection, PropertyRow, PropertySection } from './PropertyGroup'
import type { ForkCommand } from './ForkWorkspace'
import type { ForkState } from '../protocol'
import { useMotionStore } from '../store'

const auxModes = ['正常：拟合模型', '静默：锁定初始姿态', '归零：单位旋转', '回放：录制数据', '旧版：世界空间解算']

export function ForkAdvancedMotion({ state, command }: { state: ForkState; command: ForkCommand }) {
  const actors = useMotionStore(model => model.actors.actors)
  const playing = useMotionStore(model => model.player.state !== 'stopped')
  const [token, setToken] = useState(0)
  const arm = state.arms.find(item => item.token === token) ?? state.arms.find(item => item.primary) ?? state.arms[0]
  const armCommand = (operation: number, value: number) => {
    if (arm) void command('motion.arm', { actorToken: arm.token, operation, value })
  }
  return (
    <PropertySection divided>
      <PropertyRow label="站位调整后重置动态骨（实验）">
        <Checkbox aria-label="站位调整后重置动态骨" checked={state.placementReset}
          onCheckedChange={value => void command('motion.resetPhysics', { value: value === true })} />
      </PropertyRow>
      <PropertyRow label="载入动作前初始化骨骼">
        <Checkbox aria-label="载入动作前初始化骨骼" checked={state.initializeBones}
          onCheckedChange={value => void command('motion.initializeBones', { value: value === true })} />
      </PropertyRow>
      <PropertyRow label="手臂辅助骨模式" description="仅控制主角色的辅助骨解算">
        <select aria-label="手臂辅助骨模式" className="h-8 rounded-md border bg-background px-2 text-xs" value={state.auxMode}
          onChange={event => void command('motion.auxMode', { value: Number(event.currentTarget.value) })}>
          {auxModes.map((label, index) => <option key={label} value={index}>{label}</option>)}
        </select>
      </PropertyRow>
      <PropertyRow label="手腕诊断" description={['', '等待当前角色姿态', '已记录到日志', '日志服务不可用'][state.wristStatus]}>
        <Button size="sm" variant="outline" disabled={!playing} onClick={() => void command('motion.wristReport')}>记录手腕诊断</Button>
      </PropertyRow>
      <PropertyRow label="校正角色">
        <select aria-label="校正角色" className="h-8 rounded-md border bg-background px-2 text-xs" value={arm?.token ?? 0}
          disabled={!arm} onChange={event => setToken(Number(event.currentTarget.value))}>
          {!arm && <option value={0}>尚未识别角色</option>}
          {state.arms.map(item => <option key={item.token} value={item.token}>{actors.find(actor => actor.token === item.token)?.label ?? item.token}</option>)}
        </select>
      </PropertyRow>
      {arm && <>
        <PropertyRow label="前臂扭转分担（实验）">
          <NumberField key={arm.token} ariaLabel="前臂扭转分担" value={arm.share} min={-1} max={1} step={0.01}
            onCommit={value => armCommand(0, value)} />
        </PropertyRow>
        <PropertyRow label="前臂轴向校正（实验）">
          <div className="flex flex-wrap items-center gap-2">
            <NumberField key={arm.token} ariaLabel="前臂轴向校正" value={arm.degrees} min={-90} max={90} step={0.1}
              decimals={1} unit="°" disabled={arm.locked} onCommit={value => armCommand(1, value)} />
            <Button size="sm" variant="outline" disabled={arm.locked} onClick={() => armCommand(1, 45)}>设为 +45°</Button>
            <Button size="sm" variant="outline" disabled={arm.locked} onClick={() => armCommand(2, 0)}>重置前臂校正</Button>
          </div>
        </PropertyRow>
        <PropertyRow label={arm.primary ? '锁定并保存前臂校正' : '锁定当前角色前臂校正'}
          description={arm.primary ? state.armNotice : !arm.bound ? '校正会在该角色绑定动作后生效。' : undefined}>
          <Checkbox aria-label="锁定前臂校正" checked={arm.locked} onCheckedChange={value => armCommand(3, value === true ? 1 : 0)} />
        </PropertyRow>
      </>}
    </PropertySection>
  )
}

export function ForkPropNodes({ state, command }: {
  state: ForkState; command: ForkCommand
}) {
  const [filter, setFilter] = useState('')
  const query = filter.trim().toLowerCase()
  const models = new Map<string, { label: string; parts: ForkState['props'] }>()
  for (const prop of state.props) {
    const name = prop.modelName || prop.name
    const identity = [prop.actor, name, prop.name].join(' ')
    const penlight = /penlight|荧光棒/i.test(identity)
    if (prop.moving === false || (prop.moving === undefined && prop.timeline && !penlight && !/手持|移动|轨道|摇臂|trolley|dolly|crane|角色位|constraint|baton|灯柱|movable/i.test(identity))) continue
    const key = penlight ? 'penlight' : prop.modelId ? String(prop.modelId) : prop.key
    const existing = models.get(key)
    if (existing) { existing.parts.push(prop); continue }
    const label = penlight ? '荧光棒' : /trolley/i.test(name) ? '移动箱' : /dolly/i.test(name) ? '轨道摄影车'
      : /crane/i.test(name) ? '摇臂' : /baton|灯柱/i.test(identity) ? '舞台灯柱' : name
    models.set(key, { label, parts: [prop] })
  }
  const canEdit = (prop: ForkState['props'][number]) => state.propsEditable && prop.editable && (!prop.timeline || state.active)
  const [expanded, setExpanded] = useState<Record<string, boolean>>({})
  return (
    <CollapsiblePropertySection title="移动资产" summary={`已识别 ${models.size} 项`} defaultOpen>
      <div className="space-y-3">
        <div className="flex flex-wrap items-center gap-2">
          <Button size="sm" variant="outline" disabled={!state.propsCanScan || state.propsScanning}
            onClick={() => void command('stage.propsScan')}><RiRefreshFill className="size-3.5" />{state.propsScanning ? '正在扫描道具…' : '重新扫描道具'}</Button>
          <div className="relative"><RiSearchFill className="pointer-events-none absolute left-2 top-1/2 size-3.5 -translate-y-1/2 text-muted-foreground" />
            <Input className="h-8 w-60 max-w-full pl-7 text-xs" placeholder="搜索物品或部件" aria-label="搜索物品或部件"
              value={filter} onChange={event => setFilter(event.currentTarget.value)} /></div>
        </div>
        {!state.propsEditable && <p className="text-xs text-muted-foreground">当前可查看和扫描，接管后可调整显隐。</p>}
        {state.propsNotice && <p role="status" className="text-xs text-muted-foreground">{state.propsNotice}</p>}
        <section aria-label="移动资产" className="space-y-2">
          {(() => {
            const items = [...models].filter(([, model]) =>
              [model.label, ...model.parts.flatMap(prop => [prop.actor, prop.name])].some(value => value.toLowerCase().includes(query)))
            return (<>
                {!items.length && <p className="text-xs text-muted-foreground">{query ? '没有匹配的物品。' : '尚未识别到物品。'}</p>}
                {items.map(([key, model]) => {
                  const hidden = model.parts.filter(part => part.hidden).length
                  return (
                    <div key={key} className="rounded-md border bg-background/60 px-2">
                      <div className="flex flex-wrap items-center gap-2 py-1 text-xs">
                        <Button size="sm" variant="ghost" className="mr-auto h-auto min-w-0 whitespace-normal px-1"
                          aria-expanded={!!expanded[key]} onClick={() => setExpanded(current => ({ ...current, [key]: !current[key] }))}>
                          {expanded[key] ? '▾' : '▸'} {model.label} · {model.parts.length} 个部件
                        </Button>
                        <span className="text-muted-foreground">{hidden === model.parts.length ? '已隐藏' : hidden ? '部分隐藏' : '显示'}</span>
                        <label className="flex items-center gap-1.5">
                          <Checkbox aria-label={`隐藏整组 ${model.label}`} disabled={!model.parts.every(canEdit)}
                            checked={hidden === model.parts.length || (hidden > 0 && 'indeterminate')}
                            onCheckedChange={value => void command('stage.propGroup', { keys: model.parts.map(part => part.key), value: value === true })} />
                          隐藏整组
                        </label>
                      </div>
                      {expanded[key] && model.parts.map(prop => (
                        <label key={prop.key} className="flex items-center justify-between gap-2 border-t py-2 pl-2 text-xs">
                          <span className="min-w-0 break-all">{prop.name} <span className="text-muted-foreground">{prop.actor}</span></span>
                          <span className="flex shrink-0 items-center gap-1.5">
                            <Checkbox aria-label={`隐藏 ${prop.actor} ${prop.name}`} checked={prop.hidden}
                              disabled={!canEdit(prop)} onCheckedChange={value =>
                                void command('stage.prop', { actorToken: prop.actorToken, key: prop.key, value: value === true })} />
                            隐藏
                          </span>
                        </label>
                      ))}
                    </div>
                  )
                })}
            </>)
          })()}
        </section>
      </div>
    </CollapsiblePropertySection>
  )
}

export function ForkCameraOptions({ state, command }: { state: ForkState; command: ForkCommand }) {
  return <>
    <PropertyRow label="镜头随主角色朝向">
      <Checkbox aria-label="镜头随主角色朝向" checked={state.followYaw}
        onCheckedChange={value => void command('camera.followYaw', { value: value === true })} />
    </PropertyRow>
    {state.active && <PropertyRow label="关闭 Live 景深（测试）">
      <Checkbox aria-label="关闭 Live 景深" checked={state.liveDof}
        onCheckedChange={value => void command('live.dof', { value: value === true })} />
    </PropertyRow>}
  </>
}
