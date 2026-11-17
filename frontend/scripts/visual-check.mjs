// Headless visual verification of the standalone GakuMotion preview UI using
// the system Edge browser (WebView2's browser). Serves dist-preview/ locally,
// drives the mock-backed app with ?preview=1, and captures each page + theme
// so rendering can be inspected. Not a game or native WebView2 test.
import { chromium } from 'playwright'
import http from 'node:http'
import { readFile, mkdir, readdir } from 'node:fs/promises'
import path from 'node:path'

const dist = path.resolve(import.meta.dirname, '../dist-preview')
const out = path.resolve(import.meta.dirname, '../visual-check')
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
const page = await browser.newPage({ viewport: { width: 1024, height: 768 } })
const errors = []
page.on('pageerror', (e) => errors.push(`pageerror: ${e.message}`))
page.on('console', (m) => {
  if (m.type() === 'error') errors.push(`console: ${m.text()}`)
})

const shot = (name) => page.screenshot({ path: path.join(out, `${name}.png`) })
const selectPreviewTheme = async (value) => {
  await page.selectOption('select[aria-label="预览主题"]', String(value))
  await page.waitForTimeout(120)
}

await page.goto('http://localhost:4173/?preview=1')
await page.getByTestId('preview-toolbar').waitFor()
await page.waitForTimeout(200)
await shot('01-library-dark')

await selectPreviewTheme(1)
await shot('02-library-sapphire')
await selectPreviewTheme(2)
await shot('03-library-paper')
await selectPreviewTheme(3)
await shot('04-library-apple-light')
await selectPreviewTheme(4)
await shot('05-library-apple-dark')
await selectPreviewTheme(0)

await page.getByRole('button', { name: '动作' }).click()
await page.waitForTimeout(120)
await shot('06-motion')

await page.getByRole('button', { name: '镜头' }).click()
await page.waitForTimeout(120)
await shot('07-camera')

await page.getByRole('button', { name: '舞台' }).click()
await page.waitForTimeout(120)
await shot('08-stage')

await page.getByRole('button', { name: '设置' }).click()
await page.waitForTimeout(120)
await shot('09-settings')

await page.getByRole('switch', { name: '开发者模式' }).click()
await page.getByRole('button', { name: '[开发]' }).click()
await page.waitForTimeout(120)
await shot('10-developer')

await page.getByRole('button', { name: '模拟禁用控制' }).click()
await page.getByTestId('permission-gate').waitFor()
await shot('11-permission-gate')

console.log('CAPTURED', (await readdir(out)).filter((f) => f.endsWith('.png')).length, 'screenshots')
console.log('RUNTIME_ERRORS', errors.length ? errors : 'none')

await browser.close()
server.close()
if (errors.length > 0) process.exitCode = 1
