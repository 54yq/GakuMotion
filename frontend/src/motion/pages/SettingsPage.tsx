import { Label } from '@/components/ui/label'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { Separator } from '@/components/ui/separator'
import { Switch } from '@/components/ui/switch'
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from '@/components/ui/table'
import { motionStore, useMotionStore } from '../store'
import { PHYSICS_BACKENDS } from '../protocol'
import { PendingBadge } from '../components/Unsupported'

const HOTKEY_ROWS: Array<[string, string, string]> = [
  ['Ctrl + M', '唤出控制面板', '打开或唤回 GakuMotion 控制窗口'],
  ['F9', '播放 / 暂停 / 继续', '控制当前曲目；暂停后从当前位置继续'],
  ['F10', '停止并恢复游戏状态', '停止表演，复位进度并恢复原生角色控制'],
  [', / .', '上一曲 / 下一曲', '切换播放列表中的曲目'],
  ['- / =', '后退 / 前进 5 秒', '相对当前位置跳转'],
  ['Shift + F11', '重新加载骨骼微调', '从磁盘重新载入骨骼覆盖设置'],
  ['Ctrl + F11', '切换骨骼微调启用', '开启或暂时旁路骨骼覆盖'],
]

export function SettingsPage() {
  const physicsBackend = useMotionStore((state) => state.physicsBackend)
  const developerMode = useMotionStore((state) => state.developerMode)
  const physicsSupported = useMotionStore((state) => state.capabilities.includes('settings.setPhysics'))

  return (
    <div className="space-y-5" data-testid="settings-page">
      <section className="space-y-2">
        <h3 className="text-sm font-medium">物理引擎</h3>
        <div className="flex items-center gap-2">
          <Select
            value={String(physicsBackend)}
            disabled={!physicsSupported}
            onValueChange={(value) => void motionStore.setPhysicsBackend(Number(value))}
          >
            <SelectTrigger className="h-8 w-[260px]" aria-label="物理引擎">
              <SelectValue />
            </SelectTrigger>
            <SelectContent>
              {PHYSICS_BACKENDS.map((label, index) => (
                <SelectItem key={label} value={String(index)}>
                  {label}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
          {!physicsSupported && <PendingBadge />}
        </div>
        <p className="text-xs text-muted-foreground">选择在本次运行中生效；重启或控制文件变更后读取 control.json 的设置。</p>
        {physicsBackend === 2 && (
          <p className="text-sm text-warning">
            已选择原生物理实验；原生接口不可用时回退到内建物理。选择后端不代表原生物理已实际运行。
          </p>
        )}
        {physicsBackend === 1 && (
          <p className="text-xs text-muted-foreground">内建弹簧骨为保底方案，不包含官方碰撞模型。</p>
        )}
      </section>

      <Separator />

      <section className="space-y-2">
        <h3 className="text-sm font-medium">常用快捷键</h3>
        <div className="rounded-md border">
          <Table>
            <TableHeader>
              <TableRow>
                <TableHead className="w-[120px]">快捷键</TableHead>
                <TableHead className="w-[170px]">功能</TableHead>
                <TableHead>说明</TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              {HOTKEY_ROWS.map(([key, func, desc]) => (
                <TableRow key={key}>
                  <TableCell className="font-mono text-xs text-primary">{key}</TableCell>
                  <TableCell className="text-sm">{func}</TableCell>
                  <TableCell className="text-sm text-muted-foreground">{desc}</TableCell>
                </TableRow>
              ))}
            </TableBody>
          </Table>
        </div>
      </section>

      <Separator />

      <section className="space-y-2">
        <h3 className="text-sm font-medium">开发者选项</h3>
        <div className="flex items-center gap-2">
          <Switch
            id="developer-mode"
            checked={developerMode}
            onCheckedChange={(checked) => motionStore.setDeveloperMode(checked)}
          />
          <Label htmlFor="developer-mode">开发者模式</Label>
        </div>
        <p className="text-xs text-muted-foreground">
          开启后在侧边栏显示 [开发] 页，包含诊断、鞋底测量、探针与实验性开关。会话内有效，不持久化。
        </p>
      </section>
    </div>
  )
}
