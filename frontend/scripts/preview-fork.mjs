import { readFile, writeFile, mkdir, readdir } from 'node:fs/promises'
import { createHash } from 'node:crypto'
import { resolve, join } from 'node:path'
import { pathToFileURL } from 'node:url'
import { chromium } from 'playwright'

const [preview, output, executablePath] = process.argv.slice(2)
if (!preview || !output) throw new Error('Pass the built preview directory and the design output directory.')
const previewDir = resolve(preview)
const outputDir = resolve(output)
await mkdir(outputDir, { recursive: true })
const html = await readFile(join(previewDir, 'index.html'), 'utf8')
const jsPath = html.match(/<script type="module"[^>]*src="([^"]+)"><\/script>/)?.[1]
const cssPath = html.match(/<link rel="stylesheet"[^>]*href="([^"]+)">/)?.[1]
if (!jsPath || !cssPath) throw new Error('Expected a single local JS bundle and stylesheet.')
const [js, css] = await Promise.all([readFile(join(previewDir, jsPath), 'utf8'), readFile(join(previewDir, cssPath), 'utf8')])
let inlineJs = js.replace(/<\/script/gi, '<\\/script')
for (const asset of await readdir(join(previewDir, 'assets'))) {
  if (!asset.endsWith('.svg')) continue
  const image = await readFile(join(previewDir, 'assets', asset))
  const dataUrl = 'data:image/svg+xml;base64,' + image.toString('base64')
  inlineJs = inlineJs.replaceAll('./assets/' + asset, dataUrl).replaceAll('assets/' + asset, dataUrl).replaceAll(asset, dataUrl)
}
const parserHash = createHash('sha256').update(inlineJs).digest('base64')
const inline = html.replace("script-src 'self'", `script-src 'self' 'sha256-${parserHash}'`)
  .replace(/<script type="module"[^>]*src="[^"]+"><\/script>/, () => `<script type="module">${inlineJs}</script>`)
  .replace(/<link rel="stylesheet"[^>]*href="[^"]+">/, () => `<style>${css}</style>`)
const entry = join(previewDir, 'Open-Preview.html')
await writeFile(entry, inline, 'utf8')

const browser = await chromium.launch({ executablePath, headless: true })
try {
  const page = await browser.newPage({ viewport: { width: 1040, height: 880 }, deviceScaleFactor: 1 })
  const errors = []
  page.on('pageerror', error => errors.push(error.message))
  await page.goto(pathToFileURL(entry).href + '?preview=1')
  await page.getByTestId('library-page').waitFor()
  await page.getByRole('button', { name: 'Live 接管中', exact: true }).waitFor()
  await page.getByText('Starlight Step', { exact: true }).dblclick()
  await page.getByRole('button', { name: '暂停', exact: true }).click()
  const progress = page.getByRole('slider', { name: '播放进度', exact: true }).first()
  await progress.waitFor()
  await page.getByRole('img', { name: 'GakuMotion', exact: true }).evaluate(img => img.decode())
  const bar = await page.getByTestId('transport-bar').boundingBox()
  if (!bar) throw new Error('The transport bar is not visible.')
  await page.mouse.click(bar.x + bar.width * 0.38, bar.y + 1)
  await page.mouse.move(bar.x + bar.width * 0.38, bar.y + 1)
  await page.screenshot({ path: join(outputDir, '01-library-and-transport.png') })
  await page.getByRole('button', { name: '精细控制', exact: true }).click()
  await page.screenshot({ path: join(outputDir, '04-precision-controls.png') })
  await page.keyboard.press('Escape')
  await page.getByRole('button', { name: '多人站位', exact: true }).click()
  await page.getByRole('heading', { name: '副角色 2' }).waitFor()
  await page.screenshot({ path: join(outputDir, '02-formation.png') })
  await page.getByRole('button', { name: 'Live 接管中', exact: true }).click()
  await page.getByRole('button', { name: '退出接管并恢复 Live', exact: true }).waitFor()
  await page.screenshot({ path: join(outputDir, '03-live-takeover.png') })
  await page.getByRole('button', { name: '退出接管并恢复 Live', exact: true }).click()
  await page.getByRole('button', { name: '启动 Live 接管', exact: true }).waitFor()
  if (errors.length) throw new Error(errors.join('\n'))
  console.log('Design smoke: library, fixed transport, formation navigation and Live takeover toggle verified.')
} finally {
  await browser.close()
}
