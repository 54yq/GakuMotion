import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import path from 'node:path'

// Relative base so the built assets resolve under the WebView2 virtual-host
// folder mapping (https://gakumotion.invalid/) fully offline.
// When built with `--mode preview`, outputs to `dist-preview` so standalone
// browser preview builds never overwrite or pollute production `dist/`.
export default defineConfig(({ mode }) => ({
  base: './',
  plugins: [react()],
  resolve: {
    alias: { '@': path.resolve(import.meta.dirname, './src') },
  },
  build: {
    outDir: mode === 'preview' ? 'dist-preview' : 'dist',
    emptyOutDir: true,
    target: 'chrome110',
    sourcemap: false,
  },
  server: {
    port: 5173,
    strictPort: true,
  },
}))
