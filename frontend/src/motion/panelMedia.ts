import { useEffect, useState, useSyncExternalStore } from 'react'

const artworkCache = new Map<string, Promise<Blob | undefined>>()
const artworkChanged = new EventTarget()

function openArtwork(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open('gakumotion-artwork', 1)
    request.onupgradeneeded = () => request.result.createObjectStore('images')
    request.onsuccess = () => resolve(request.result)
    request.onerror = () => reject(request.error)
  })
}

async function readArtwork(key: string): Promise<Blob | undefined> {
  const db = await openArtwork()
  try {
    return await new Promise((resolve, reject) => {
      const request = db.transaction('images').objectStore('images').get(key)
      request.onsuccess = () => resolve(request.result as Blob | undefined)
      request.onerror = () => reject(request.error)
    })
  } finally {
    db.close()
  }
}

export async function saveArtwork(key: string, value?: Blob): Promise<void> {
  const db = await openArtwork()
  try {
    await new Promise<void>((resolve, reject) => {
      const transaction = db.transaction('images', 'readwrite')
      const store = transaction.objectStore('images')
      if (value) store.put(value, key)
      else store.delete(key)
      transaction.oncomplete = () => resolve()
      transaction.onabort = () => reject(transaction.error)
    })
  } finally {
    db.close()
  }
  artworkCache.delete(key)
  artworkChanged.dispatchEvent(new Event(key))
}

export function useArtwork(key: string | undefined) {
  const [result, setResult] = useState<{ key: string; url: string }>()
  useEffect(() => {
    if (!key) return
    let current = true
    let url: string | undefined
    let generation = 0
    const refresh = () => {
      const requestGeneration = ++generation
      let pending = artworkCache.get(key)
      if (!pending) {
        if (artworkCache.size >= 96) {
          const oldest = artworkCache.keys().next().value
          if (oldest !== undefined) artworkCache.delete(oldest)
        }
        pending = readArtwork(key).catch(() => {
          if (artworkCache.get(key) === pending) artworkCache.delete(key)
          return undefined
        })
        artworkCache.set(key, pending)
      }
      void pending.then((blob) => {
        if (!current || requestGeneration !== generation) return
        if (url) URL.revokeObjectURL(url)
        url = blob ? URL.createObjectURL(blob) : undefined
        setResult(url ? { key, url } : undefined)
      })
    }
    artworkChanged.addEventListener(key, refresh)
    refresh()
    return () => {
      current = false
      artworkChanged.removeEventListener(key, refresh)
      if (url) URL.revokeObjectURL(url)
    }
  }, [key])
  return result?.key === key ? result?.url : undefined
}

const layoutKey = 'gakumotion.transport-layout'
const layoutChanged = new EventTarget()

function readTransportLayout(): 'classic' | 'stacked' {
  try {
    return localStorage.getItem(layoutKey) === 'stacked' ? 'stacked' : 'classic'
  } catch {
    return 'classic'
  }
}

function subscribeTransportLayout(callback: () => void) {
  layoutChanged.addEventListener('change', callback)
  window.addEventListener('storage', callback)
  return () => {
    layoutChanged.removeEventListener('change', callback)
    window.removeEventListener('storage', callback)
  }
}

export function useTransportLayout() {
  return useSyncExternalStore(subscribeTransportLayout, readTransportLayout)
}

export function setTransportLayout(layout: 'classic' | 'stacked') {
  localStorage.setItem(layoutKey, layout)
  layoutChanged.dispatchEvent(new Event('change'))
}
