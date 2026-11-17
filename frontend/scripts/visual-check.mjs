// Headless visual verification of the standalone GakuMotion preview UI using
// the system Edge browser (WebView2's browser). Serves dist-preview/ locally,
// drives the mock-backed app with ?preview=1, and captures each page + theme +
// Player Bar popovers + context menu + multi-select + zoom state so rendering
// can be inspected visually.
import { chromium } from 'playwright'
import http from 'node:http'
import { readFile, mkdir, readdir, rm } from 'node:fs/promises'
import path from 'node:path'

const dist = path.resolve(import.meta.dirname, '../dist-preview')
const out = path.resolve(import.meta.dirname, '../visual-check')
await rm(out, { recursive: true, force: true })
await mkdir(out, { recursive: true })

const mime = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.svg': 'image/svg+xml' }
const server = http.createServer(async (req, res) => {
  let p = (req.url ?? '/').split('?')[0]
  if (p === '/') p = '/index.html'
  try {
    const data = await readFile(path.join(dist, p))
    res.writeHead(200, { 'Content-Type': mime[path.extname(p)] ?? 'application/octet-stream' })
    res.end(data)
  } catch {
    res.writeHead(404)
    res.end('not found')
  }
})
await new Promise((resolve) => server.listen(4173, resolve))

const browser = await chromium.launch({ channel: 'msedge', headless: true })
const errors = []
try {
  const page = await browser.newPage({ viewport: { width: 1200, height: 800 } })
  page.on('pageerror', (e) => errors.push(`pageerror: ${e.message}`))
  page.on('console', (m) => {
    if (m.type() === 'error') errors.push(`console: ${m.text()}`)
  })

  const shot = (name) => page.screenshot({ path: path.join(out, `${name}.png`) })
  const sidebar = page.getByTestId('motion-sidebar')
  const navTo = async (label) => {
    await sidebar.getByRole('button', { name: label, exact: true }).click()
    await page.waitForTimeout(140)
  }
  const selectPreviewTheme = async (value) => {
    await page.selectOption('select[aria-label="预览主题"]', String(value))
    await page.waitForTimeout(140)
  }

  await page.goto('http://localhost:4173/?preview=1')
  await page.getByTestId('preview-toolbar').waitFor()
  await page.waitForTimeout(200)

  // Play first track and hover second track to verify inline quick actions
  await page.getByTestId('library-row-hikari-no-saki').dblclick()
  await page.waitForTimeout(200)
  await page.getByTestId('library-row-starlight-step').hover()
  await page.waitForTimeout(120)
  await shot('01-library-dark')

  await selectPreviewTheme(1)
  await shot('02-library-sapphire')
  await selectPreviewTheme(2)
  await shot('03-library-paper')

  // Apple Light with right-click context menu on a row
  await selectPreviewTheme(3)
  await page.getByTestId('library-row-aoi-haru').click({ button: 'right' })
  await page.getByTestId('library-context-menu').waitFor()
  await page.waitForTimeout(120)
  await shot('04-library-apple-light-menu')
  await page.keyboard.press('Escape')

  // Apple Dark with selected card row + hovered row
  await selectPreviewTheme(4)
  await page.getByTestId('library-row-aoi-haru').click()
  await page.getByTestId('library-row-starlight-step').hover()
  await page.waitForTimeout(120)
  await shot('05-library-apple-dark-selected')

  await selectPreviewTheme(0)
  // Precision Control Panel open over pure list library
  await page.getByTestId('precision-control-trigger').click()
  await page.waitForTimeout(180)
  await shot('06-library-precision')
  await page.getByRole('button', { name: '关闭精细控制' }).click()

  await navTo('动作')
  await shot('07-motion')

  await navTo('镜头')
  await shot('08-camera')

  await navTo('舞台')
  await shot('09-stage')

  await navTo('设置')
  await shot('10-settings')

  await page.getByRole('switch', { name: '开发者模式' }).click()
  await navTo('[开发]')
  await shot('11-developer')

  // Compact 1024x768 @ 125% scale check in Apple Light with vertical volume popover open
  await page.setViewportSize({ width: 1024, height: 768 })
  await selectPreviewTheme(3)
  await navTo('曲库')
  await page.selectOption('select[aria-label="预览缩放"]', '1.25')
  await page.getByRole('button', { name: '音量控制' }).click()
  await page.waitForTimeout(150)
  await shot('12-compact-zoom125-volume')
  await page.selectOption('select[aria-label="预览缩放"]', '1')

  await page.getByRole('button', { name: '模拟禁用控制' }).click()
  await page.getByTestId('permission-gate').waitFor()
  await shot('13-permission-gate')

  console.log('CAPTURED', (await readdir(out)).filter((f) => f.endsWith('.png')).length, 'screenshots')
  console.log('RUNTIME_ERRORS', errors.length ? errors : 'none')
} finally {
  await browser.close()
  server.close()
}
if (errors.length > 0) process.exitCode = 1
