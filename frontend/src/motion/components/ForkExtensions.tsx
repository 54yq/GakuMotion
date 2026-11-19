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

export function ForkPropNodes({ state, command }: { state: ForkState; command: ForkCommand }) {
  const [filter, setFilter] = useState('')
  const query = filter.trim().toLowerCase()
  const props = state.props.filter(prop => `${prop.actor} ${prop.name}`.toLowerCase().includes(query))
  return (
    <CollapsiblePropertySection title="自定义道具节点" summary={`已识别 ${state.props.length} 项`} defaultOpen>
      <div className="space-y-2">
        <Input className="h-7 max-w-60 text-xs" placeholder="过滤道具或角色" aria-label="过滤道具节点"
          value={filter} onChange={event => setFilter(event.currentTarget.value)} />
        {!state.propsEditable && <p className="text-xs text-muted-foreground">当前场景可查看道具，显隐调整暂不可用。</p>}
        <div className="max-h-72 divide-y overflow-auto rounded-md border bg-background/60">
          {!props.length && <p className="p-3 text-xs text-muted-foreground">{query ? '没有匹配道具。' : '尚未识别到角色道具。'}</p>}
          {props.map(prop => (
            <label className="flex items-center justify-between gap-3 px-3 py-2 text-xs" key={prop.key}>
              <span>{prop.actor} / {prop.name}</span>
              <span className="flex items-center gap-2 text-muted-foreground">
                <Checkbox aria-label={`隐藏 ${prop.actor} ${prop.name}`} checked={prop.hidden}
                  disabled={!state.propsEditable || !prop.editable} onCheckedChange={value =>
                    void command('stage.prop', { actorToken: prop.actorToken, key: prop.key, value: value === true })} />
                隐藏
              </span>
            </label>
          ))}
        </div>
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
