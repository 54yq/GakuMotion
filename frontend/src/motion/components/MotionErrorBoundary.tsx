import * as React from 'react'
import { AlertTriangle, RefreshCw, RotateCcw } from 'lucide-react'
import { Button } from '@/components/ui/button'
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card'

export interface MotionErrorBoundaryProps {
  scope: 'app' | 'page'
  resetKey?: string | number
  onResync?: () => void | Promise<void>
  onNavigateLibrary?: () => void
  onRestart?: () => void | Promise<void>
  children: React.ReactNode
}

interface MotionErrorBoundaryState {
  error: Error | null
  componentStack: string
  showDetails: boolean
}

export class MotionErrorBoundary extends React.Component<
  MotionErrorBoundaryProps,
  MotionErrorBoundaryState
> {
  state: MotionErrorBoundaryState = {
    error: null,
    componentStack: '',
    showDetails: false,
  }

  static getDerivedStateFromError(error: unknown): Partial<MotionErrorBoundaryState> {
    const normalized = error instanceof Error ? error : new Error(String(error))
    return { error: normalized, showDetails: false }
  }

  componentDidCatch(error: unknown, info: React.ErrorInfo): void {
    const normalized = error instanceof Error ? error : new Error(String(error))
    this.setState({
      error: normalized,
      componentStack: info.componentStack ?? '',
    })
  }

  componentDidUpdate(prevProps: MotionErrorBoundaryProps): void {
    if (this.state.error && this.props.resetKey !== prevProps.resetKey) {
      this.setState({ error: null, componentStack: '', showDetails: false })
    }
  }

  private handleRetry = (): void => {
    this.setState({ error: null, componentStack: '', showDetails: false })
  }

  private handleResync = (): void => {
    this.setState({ error: null, componentStack: '', showDetails: false })
    if (this.props.onResync) void this.props.onResync()
  }

  private handleReturnLibrary = (): void => {
    this.setState({ error: null, componentStack: '', showDetails: false })
    if (this.props.onNavigateLibrary) this.props.onNavigateLibrary()
  }

  private handleRestart = (): void => {
    this.setState({ error: null, componentStack: '', showDetails: false })
    if (this.props.onRestart) void this.props.onRestart()
  }

  render(): React.ReactNode {
    const { error, componentStack, showDetails } = this.state
    if (!error) return this.props.children

    const isApp = this.props.scope === 'app'
    const detailsText = [error.stack || error.message, componentStack].filter(Boolean).join('\n')

    return (
      <div
        className={
          isApp
            ? 'flex h-full items-center justify-center bg-background p-6 text-foreground'
            : 'flex h-full items-center justify-center p-4'
        }
        data-testid="motion-error-boundary"
      >
        <Card className="w-full max-w-lg border-destructive/40">
          <CardHeader className="flex flex-row items-center gap-2 space-y-0 pb-2">
            <AlertTriangle className="size-5 shrink-0 text-destructive" />
            <CardTitle className="text-base">
              {isApp ? 'GakuMotion 界面遇到异常' : '当前页面渲染遇到异常'}
            </CardTitle>
          </CardHeader>
          <CardContent className="space-y-3 text-sm">
            <p className="text-muted-foreground">
              {isApp
                ? '已阻止白屏扩散，你可以重试渲染或重新建立原生桥接。'
                : '导航栏与底部播放控制仍然可用。你可以重试、重新同步后端状态，或返回曲库。'}
            </p>
            <p className="rounded border bg-muted/50 px-2.5 py-1.5 font-mono text-xs text-destructive break-all">
              {error.message || '未知渲染错误'}
            </p>
            {detailsText && (
              <div>
                <Button
                  type="button"
                  variant="ghost"
                  size="sm"
                  className="h-7 px-2 text-xs"
                  onClick={() => this.setState((prev) => ({ ...prev, showDetails: !prev.showDetails }))}
                >
                  {showDetails ? '隐藏错误堆栈' : '查看错误堆栈'}
                </Button>
                {showDetails && (
                  <pre className="mt-1.5 max-h-40 overflow-auto rounded border bg-muted p-2 text-xs whitespace-pre-wrap">
                    {detailsText}
                  </pre>
                )}
              </div>
            )}
            <div className="flex flex-wrap gap-2 pt-1">
              <Button type="button" size="sm" onClick={this.handleRetry}>
                <RotateCcw className="size-3.5" />
                重试渲染
              </Button>
              {isApp ? (
                this.props.onRestart && (
                  <Button type="button" variant="outline" size="sm" onClick={this.handleRestart}>
                    <RefreshCw className="size-3.5" />
                    重新连接 Bridge
                  </Button>
                )
              ) : (
                <>
                  {this.props.onResync && (
                    <Button type="button" variant="outline" size="sm" onClick={this.handleResync}>
                      <RefreshCw className="size-3.5" />
                      重新同步状态
                    </Button>
                  )}
                  {this.props.onNavigateLibrary && (
                    <Button type="button" variant="outline" size="sm" onClick={this.handleReturnLibrary}>
                      返回曲库
                    </Button>
                  )}
                </>
              )}
            </div>
          </CardContent>
        </Card>
      </div>
    )
  }
}
