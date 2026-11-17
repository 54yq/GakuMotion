import { Button } from '@/components/ui/button'
import { Checkbox } from '@/components/ui/checkbox'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { motionStore, useMotionStore } from '../store'
import { ENSEMBLE_SLOT_NAMES } from '../protocol'
import { Vector3Field } from './NumberField'
import { PendingBadge } from './Unsupported'

export function EnsembleSection() {
  const ensemble = useMotionStore((state) => state.ensemble)
  const draft = useMotionStore((state) => state.ensembleDraft)
  const actors = useMotionStore((state) => state.actors.actors)
  const libraryEntries = useMotionStore((state) => state.library.entries)
  const presetName = useMotionStore((state) => state.ensemblePresetName)
  const playerState = useMotionStore((state) => state.player.state)
  const capabilities = useMotionStore((state) => state.capabilities)
  const supports = (method: string) => capabilities.includes(method)
  const draftError = motionStore.ensembleDraftError()
  const playing = playerState === 'playing'

  return (
    <div className="space-y-3 rounded-lg border p-3" data-testid="ensemble-section">
      <div className="flex flex-wrap items-center gap-3">
        <label className="flex items-center gap-2 text-sm">
          <Checkbox
            checked={ensemble.probeEnabled}
            disabled={!supports('ensemble.probe')}
            onCheckedChange={(checked) => void motionStore.setEnsembleProbe(checked === true)}
          />
          摄影槽位验证探针
        </label>
        <label className="flex items-center gap-2 text-sm">
          <Checkbox
            checked={draft.enabled}
            disabled={!supports('ensemble.set')}
            onCheckedChange={(checked) => motionStore.patchEnsembleDraft({ enabled: checked === true })}
          />
          启用多人舞台
        </label>
        {!supports('ensemble.set') && <PendingBadge />}
      </div>

      {playing && draft.enabled && (
        <p className="text-sm text-muted-foreground">播放中请先暂停再配置舞台；配置会在应用后生效。</p>
      )}

      <div className="grid gap-2 sm:grid-cols-2">
        <div className="space-y-1">
          <Label className="text-xs">主槽（音频与运镜）</Label>
          <Select
            value={String(draft.masterSlot)}
            onValueChange={(value) => motionStore.patchEnsembleDraft({ masterSlot: Number(value) })}
          >
            <SelectTrigger className="h-8"><SelectValue /></SelectTrigger>
            <SelectContent>
              {ENSEMBLE_SLOT_NAMES.map((name, index) => (
                <SelectItem key={name} value={String(index)}>{name}</SelectItem>
              ))}
            </SelectContent>
          </Select>
        </div>
        <div className="space-y-1">
          <Label className="text-xs">镜头跟随目标槽</Label>
          <Select
            value={String(draft.cameraSlot)}
            onValueChange={(value) => motionStore.patchEnsembleDraft({ cameraSlot: Number(value) })}
          >
            <SelectTrigger className="h-8"><SelectValue /></SelectTrigger>
            <SelectContent>
              {ENSEMBLE_SLOT_NAMES.map((name, index) => (
                <SelectItem key={name} value={String(index)}>{name}</SelectItem>
              ))}
            </SelectContent>
          </Select>
        </div>
      </div>

      {draft.slots.map((slot, index) => {
        const actorKnown = slot.actorToken > 0 && actors.some((actor) => actor.token === slot.actorToken)
        const selectedActorValue = actorKnown ? String(slot.actorToken) : '0'
        const selectedMotionValue = slot.motionKey !== '' ? slot.motionKey : '__none__'
        const motionInLibrary = slot.motionKey !== '' && libraryEntries.some((entry) => entry.id === slot.motionKey)
        return (
          <div key={index} className="space-y-2 rounded-md border p-2.5">
            <div className="flex flex-wrap items-center gap-3">
              <label className="flex items-center gap-2 text-sm font-medium">
                <Checkbox
                  checked={slot.enabled}
                  onCheckedChange={(checked) => motionStore.patchEnsembleSlot(index, { enabled: checked === true })}
                />
                使用 {ENSEMBLE_SLOT_NAMES[index]}
              </label>
              <span className="text-xs text-muted-foreground">{slot.actorHint || '不绑定角色'}</span>
            </div>
            <div className="grid gap-2 sm:grid-cols-2">
              <div className="space-y-1">
                <Label className="text-xs">{ENSEMBLE_SLOT_NAMES[index]} 绑定角色</Label>
                <Select
                  value={selectedActorValue}
                  onValueChange={(value) => {
                    const token = Number(value)
                    const actor = actors.find((candidate) => candidate.token === token)
                    motionStore.patchEnsembleSlot(index, {
                      actorToken: token,
                      actorHint: actor ? actor.label : '',
                    })
                  }}
                >
                  <SelectTrigger className="h-8" aria-label={`${ENSEMBLE_SLOT_NAMES[index]} 角色`}>
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="0">不绑定角色</SelectItem>
                    {actors.map((actor) => (
                      <SelectItem key={actor.token} value={String(actor.token)}>
                        {actor.label}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>
              <div className="space-y-1">
                <Label className="text-xs">{ENSEMBLE_SLOT_NAMES[index]} 演出动作</Label>
                <Select
                  value={selectedMotionValue}
                  onValueChange={(value) =>
                    motionStore.patchEnsembleSlot(index, { motionKey: value === '__none__' ? '' : value })
                  }
                >
                  <SelectTrigger className="h-8" aria-label={`${ENSEMBLE_SLOT_NAMES[index]} 动作`}>
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="__none__">（未选择动作）</SelectItem>
                    {!motionInLibrary && slot.motionKey !== '' && (
                      <SelectItem value={slot.motionKey}>{slot.motionKey}（未在当前曲库）</SelectItem>
                    )}
                    {libraryEntries.map((entry) => (
                      <SelectItem key={entry.id} value={entry.id}>
                        {entry.title || entry.id}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>
            </div>
            <div className="grid gap-2 sm:grid-cols-2">
              <div className="space-y-1">
                <Label className="text-xs">站位偏移 (米)</Label>
                <Vector3Field
                  labelPrefix={`槽${index + 1}站位`}
                  value={slot.offset}
                  min={-10}
                  max={10}
                  step={0.01}
                  onCommit={(next) => motionStore.patchEnsembleSlot(index, { offset: next })}
                />
              </div>
              <div className="space-y-1">
                <Label className="text-xs">朝向 (度)</Label>
                <Input
                  type="number"
                  min={-180}
                  max={180}
                  step={1}
                  value={slot.yawDegrees}
                  onChange={(event) => motionStore.patchEnsembleSlot(index, { yawDegrees: Number(event.target.value) })}
                />
              </div>
            </div>
          </div>
        )
      })}

      {draftError && <p className="text-sm text-destructive">{draftError}</p>}
      {ensemble.notice && <p className="text-sm text-muted-foreground">{ensemble.notice}</p>}
      {ensemble.physics.length > 0 && (
        <p className="text-xs text-muted-foreground">物理状态：{ensemble.physics.join(' / ')}</p>
      )}

      <div className="flex flex-wrap gap-2">
        <Button
          size="sm"
          disabled={!supports('ensemble.set') || !!draftError}
          onClick={() => void motionStore.applyEnsemble()}
        >
          应用多人舞台配置
        </Button>
        <Button
          variant="outline"
          size="sm"
          disabled={!supports('ensemble.set') || !ensemble.active}
          onClick={() => void motionStore.disableEnsemble()}
        >
          停用
        </Button>
        <Button
          variant="outline"
          size="sm"
          onClick={() => motionStore.copyMasterMotionToAllSlots()}
        >
          复制主槽动作到全部槽
        </Button>
        <Button
          variant="outline"
          size="sm"
          onClick={() => motionStore.resetEnsemblePlacements()}
        >
          重置各槽站位
        </Button>
      </div>

      <div className="flex flex-wrap items-center gap-2">
        <Input
          className="h-8 w-48"
          placeholder="预设名称"
          value={presetName}
          onChange={(event) => motionStore.setEnsemblePresetName(event.target.value)}
        />
        <Button
          variant="outline"
          size="sm"
          disabled={!presetName.trim() || !supports('ensemble.presetSave')}
          onClick={() => void motionStore.saveEnsemblePreset()}
        >
          保存预设
        </Button>
        <Button
          variant="outline"
          size="sm"
          disabled={!presetName.trim() || !supports('ensemble.presetLoad')}
          onClick={() => void motionStore.loadEnsemblePreset()}
        >
          载入预设
        </Button>
      </div>
    </div>
  )
}
