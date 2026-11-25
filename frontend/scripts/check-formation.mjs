import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { chromium } from 'playwright'

const browser = await chromium.launch({ headless: true, executablePath: process.argv[2] })
try {
  const page = await browser.newPage({ viewport: { width: 1100, height: 850 } })
  const errors = []
  page.on('pageerror', error => errors.push(error.message))
  await page.route('https://motion.test/**', async route => {
    const pathname = new URL(route.request().url()).pathname
    const file = new URL(`../dist-preview${pathname === '/' ? '/index.html' : pathname}`, import.meta.url)
    const mime = { html: 'text/html;charset=utf-8', js: 'text/javascript', css: 'text/css', svg: 'image/svg+xml' }
    await route.fulfill({ body: await readFile(file), contentType: mime[file.pathname.split('.').at(-1)] })
  })
  await page.goto('https://motion.test/?preview=1')
  await page.getByRole('navigation', { name: '主导航' }).getByRole('button', { name: '动作', exact: true }).click()
  const trigger = page.getByTestId('motion-ensemble').getByRole('button', { name: /多人舞台/ })
  if (await trigger.getAttribute('aria-expanded') !== 'true') await trigger.click()
  const group = page.getByRole('group', { name: '演出角色槽位' })
  await group.waitFor()
  const slot = index => group.locator(`[data-follower-slot="${index}"]`)
  const motion = () => page.getByRole('combobox', { name: '动作', exact: true })
  const offset = () => page.getByRole('spinbutton', { name: '偏移 秒', exact: true })
  async function drag(from, to) {
    await slot(to).scrollIntoViewIfNeeded()
    const source = await slot(from).boundingBox()
    const target = await slot(to).boundingBox()
    assert(source && target)
    await page.mouse.move(source.x + source.width / 2, source.y + source.height / 2)
    await page.mouse.down()
    await page.mouse.move(target.x + target.width / 2, target.y + target.height / 2, { steps: 10 })
    await page.mouse.up()
  }
  await slot(0).click()
  assert.equal(await motion().inputValue(), 'starlight-step')
  await offset().fill('0.5')
  await slot(1).click()
  assert.equal(await motion().inputValue(), 'aoi-haru')
  await slot(0).click()
  assert.equal(await offset().inputValue(), '0.5')
  await drag(0, 1)
  assert.match(await slot(1).innerText(), /凛/)
  assert.match(await slot(0).innerText(), /手毬/)
  assert.equal(await slot(1).getAttribute('aria-pressed'), 'true')
  assert.equal(await motion().inputValue(), 'aoi-haru')
  assert.equal(await offset().inputValue(), '0.25')
  await page.getByRole('button', { name: '添加副角色' }).click()
  assert.match(await slot(2).innerText(), /空位/)
  await drag(1, 2)
  assert.match(await slot(1).innerText(), /空位/)
  assert.match(await slot(2).innerText(), /凛/)
  assert.equal(await motion().inputValue(), 'aoi-haru')
  assert.equal(await offset().inputValue(), '0.25')
  await page.setViewportSize({ width: 520, height: 800 })
  for (let i = 0; i < 4; i++) await page.getByRole('button', { name: '添加副角色' }).click()
  assert(await group.evaluate(row => row.scrollWidth > row.clientWidth && row.scrollLeft > 0))
  assert.deepEqual(errors, [])
  console.log('Web preview: click, draft retention, occupied swap, empty move and narrow slot scrolling passed.')
} finally {
  await browser.close()
}
