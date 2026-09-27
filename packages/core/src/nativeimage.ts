// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// packages/core/src/nativeimage.ts — nativeImage sin dependencias nativas.
//
// Códec PNG completo (colorType 0/2/3/4/6, bitDepth 1/2/4/8/16, sin interlace)
// con el zlib de Node + resize bilineal y crop. Idéntico en Linux y Windows.
// JPEG: `getSize` (cabecera SOF) y `toJPEG` (devuelve los bytes si la fuente ya
// es JPEG); resize de JPEG no soportado.

import { inflateSync, deflateSync } from 'node:zlib'
import * as fs from 'node:fs'

export interface Size {
  width: number
  height: number
}
export interface Rectangle {
  x: number
  y: number
  width: number
  height: number
}
export interface ResizeOptions {
  width?: number
  height?: number
  quality?: 'good' | 'better' | 'best'
}

const CRC_TABLE = (() => {
  const t = new Uint32Array(256)
  for (let n = 0; n < 256; n++) {
    let c = n
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1
    t[n] = c >>> 0
  }
  return t
})()

function crc32(buf: Uint8Array): number {
  let c = 0xffffffff
  for (let i = 0; i < buf.length; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8)
  return (c ^ 0xffffffff) >>> 0
}

function paeth(a: number, b: number, c: number): number {
  const p = a + b - c
  const pa = Math.abs(p - a)
  const pb = Math.abs(p - b)
  const pc = Math.abs(p - c)
  return pa <= pb && pa <= pc ? a : pb <= pc ? b : c
}

interface Rgba {
  width: number
  height: number
  data: Uint8Array
}

/** Tamaño a partir del IHDR (aunque no se pueda decodificar). */
function pngSize(buf: Uint8Array): Size | null {
  if (buf.length < 24 || buf[0] !== 0x89 || buf[1] !== 0x50) return null
  const be = (i: number) => ((buf[i] << 24) | (buf[i + 1] << 16) | (buf[i + 2] << 8) | buf[i + 3]) >>> 0
  return { width: be(16), height: be(20) }
}

/** Decodifica PNG a RGBA (paleta y low bit-depth incluidos). */
function decodePNG(png: Uint8Array): Rgba | null {
  const sig = [137, 80, 78, 71, 13, 10, 26, 10]
  for (let i = 0; i < 8; i++) if (png[i] !== sig[i]) return null

  let pos = 8
  let width = 0
  let height = 0
  let bitDepth = 0
  let colorType = 0
  let interlace = 0
  let palette: Uint8Array | null = null
  let trns: Uint8Array | null = null
  const idat: Uint8Array[] = []
  while (pos + 8 <= png.length) {
    const len =
      (((png[pos] << 24) | (png[pos + 1] << 16) | (png[pos + 2] << 8) | png[pos + 3]) >>> 0)
    const type = String.fromCharCode(png[pos + 4], png[pos + 5], png[pos + 6], png[pos + 7])
    const data = png.subarray(pos + 8, pos + 8 + len)
    if (type === 'IHDR') {
      width = ((data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3]) >>> 0
      height = ((data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7]) >>> 0
      bitDepth = data[8]
      colorType = data[9]
      interlace = data[12]
    } else if (type === 'PLTE') {
      palette = data
    } else if (type === 'tRNS') {
      trns = data
    } else if (type === 'IDAT') {
      idat.push(data)
    } else if (type === 'IEND') {
      break
    }
    pos += 12 + len
  }
  if (interlace !== 0 || width === 0 || height === 0) return null
  if (![1, 2, 4, 8, 16].includes(bitDepth)) return null
  if (colorType !== 0 && colorType !== 2 && colorType !== 3 && colorType !== 4 && colorType !== 6)
    return null
  if (colorType === 3 && (!palette || bitDepth === 16)) return null

  const channels = colorType === 6 ? 4 : colorType === 2 ? 3 : colorType === 4 ? 2 : 1
  const bpp = Math.max(1, (channels * bitDepth) >> 3)
  const stride = Math.ceil((width * channels * bitDepth) / 8)
  const raw = (() => {
    try {
      return inflateSync(Buffer.concat(idat.map((d) => Buffer.from(d))))
    } catch {
      return null
    }
  })()
  if (!raw || raw.length < (stride + 1) * height) return null

  const maxVal = (1 << bitDepth) - 1
  const out = new Uint8Array(width * height * 4)
  const prev = new Uint8Array(stride)
  const cur = new Uint8Array(stride)

  const sample = (row: Uint8Array, index: number): number => {
    if (bitDepth === 8) return row[index]
    if (bitDepth === 16) return row[index * 2] // byte alto
    const bitPos = index * bitDepth
    const byte = row[bitPos >> 3]
    const shift = 8 - bitDepth - (bitPos & 7)
    return (byte >> shift) & maxVal
  }

  for (let y = 0; y < height; y++) {
    const filter = raw[y * (stride + 1)]
    const rowStart = y * (stride + 1) + 1
    for (let i = 0; i < stride; i++) {
      const x = raw[rowStart + i]
      const a = i >= bpp ? cur[i - bpp] : 0
      const b = prev[i]
      const c = i >= bpp ? prev[i - bpp] : 0
      let v: number
      switch (filter) {
        case 0: v = x; break
        case 1: v = x + a; break
        case 2: v = x + b; break
        case 3: v = x + ((a + b) >> 1); break
        case 4: v = x + paeth(a, b, c); break
        default: return null
      }
      cur[i] = v & 0xff
    }
    for (let x = 0; x < width; x++) {
      const o = (y * width + x) * 4
      if (colorType === 3) {
        const idx = sample(cur, x)
        const pi = idx * 3
        out[o] = palette![pi]
        out[o + 1] = palette![pi + 1]
        out[o + 2] = palette![pi + 2]
        out[o + 3] = trns && idx < trns.length ? trns[idx] : 255
      } else if (colorType === 0 || colorType === 4) {
        const g = sample(cur, x * (colorType === 4 ? 2 : 1))
        const gg = bitDepth < 8 ? Math.round((g * 255) / maxVal) : g
        out[o] = out[o + 1] = out[o + 2] = gg
        out[o + 3] = colorType === 4 ? sample(cur, x * 2 + 1) : 255
      } else if (colorType === 2) {
        out[o] = sample(cur, x * 3)
        out[o + 1] = sample(cur, x * 3 + 1)
        out[o + 2] = sample(cur, x * 3 + 2)
        out[o + 3] = 255
      } else {
        out[o] = sample(cur, x * 4)
        out[o + 1] = sample(cur, x * 4 + 1)
        out[o + 2] = sample(cur, x * 4 + 2)
        out[o + 3] = sample(cur, x * 4 + 3)
      }
    }
    prev.set(cur)
  }
  return { width, height, data: out }
}

/** Codifica RGBA a PNG (colorType 6, bitDepth 8, filtro 0). */
function encodePNG(img: Rgba): Buffer {
  const { width, height, data } = img
  const stride = width * 4
  const raw = Buffer.alloc((stride + 1) * height)
  for (let y = 0; y < height; y++) {
    raw[y * (stride + 1)] = 0
    Buffer.from(data.buffer, data.byteOffset + y * stride, stride).copy(raw, y * (stride + 1) + 1)
  }
  const idat = deflateSync(raw)
  const chunk = (type: string, body: Buffer): Buffer => {
    const len = Buffer.alloc(4)
    len.writeUInt32BE(body.length, 0)
    const t = Buffer.from(type, 'ascii')
    const crc = Buffer.alloc(4)
    crc.writeUInt32BE(crc32(Buffer.concat([t, body])), 0)
    return Buffer.concat([len, t, body, crc])
  }
  const ihdr = Buffer.alloc(13)
  ihdr.writeUInt32BE(width, 0)
  ihdr.writeUInt32BE(height, 4)
  ihdr[8] = 8
  ihdr[9] = 6
  return Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', ihdr),
    chunk('IDAT', idat),
    chunk('IEND', Buffer.alloc(0)),
  ])
}

function resizeBilinear(src: Rgba, dw: number, dh: number): Rgba {
  const out = new Uint8Array(dw * dh * 4)
  const sx = src.width / dw
  const sy = src.height / dh
  for (let y = 0; y < dh; y++) {
    const fy = Math.min(src.height - 1, (y + 0.5) * sy - 0.5)
    const y0 = Math.max(0, Math.floor(fy))
    const y1 = Math.min(src.height - 1, y0 + 1)
    const wy = fy - y0
    for (let x = 0; x < dw; x++) {
      const fx = Math.min(src.width - 1, (x + 0.5) * sx - 0.5)
      const x0 = Math.max(0, Math.floor(fx))
      const x1 = Math.min(src.width - 1, x0 + 1)
      const wx = fx - x0
      const o = (y * dw + x) * 4
      for (let c = 0; c < 4; c++) {
        const p00 = src.data[(y0 * src.width + x0) * 4 + c]
        const p10 = src.data[(y0 * src.width + x1) * 4 + c]
        const p01 = src.data[(y1 * src.width + x0) * 4 + c]
        const p11 = src.data[(y1 * src.width + x1) * 4 + c]
        out[o + c] = Math.round(
          p00 * (1 - wx) * (1 - wy) + p10 * wx * (1 - wy) + p01 * (1 - wx) * wy + p11 * wx * wy
        )
      }
    }
  }
  return { width: dw, height: dh, data: out }
}

/** Tamaño de un JPEG (marcador SOF). */
function jpegSize(buf: Uint8Array): Size | null {
  if (buf[0] !== 0xff || buf[1] !== 0xd8) return null
  let i = 2
  while (i + 9 < buf.length) {
    if (buf[i] !== 0xff) {
      i++
      continue
    }
    const marker = buf[i + 1]
    if (marker === 0xd8 || marker === 0x01 || (marker >= 0xd0 && marker <= 0xd7)) {
      i += 2
      continue
    }
    const len = (buf[i + 2] << 8) | buf[i + 3]
    if (marker >= 0xc0 && marker <= 0xcf && marker !== 0xc4 && marker !== 0xc8 && marker !== 0xcc)
      return { height: (buf[i + 5] << 8) | buf[i + 6], width: (buf[i + 7] << 8) | buf[i + 8] }
    i += 2 + len
  }
  return null
}

function detectFormat(buf: Uint8Array): 'png' | 'jpeg' | 'unknown' {
  if (buf.length > 3 && buf[0] === 0x89 && buf[1] === 0x50) return 'png'
  if (buf.length > 2 && buf[0] === 0xff && buf[1] === 0xd8) return 'jpeg'
  return 'unknown'
}

/** Imagen (estilo Electron `nativeImage`). */
export class NativeImage {
  private _rgba: Rgba | null
  private _fallback: Buffer | null
  private _format: 'png' | 'jpeg' | 'unknown'

  private constructor(rgba: Rgba | null, fallback: Buffer | null, format: NativeImage['_format']) {
    this._rgba = rgba
    this._fallback = fallback
    this._format = format
  }

  static fromBuffer(buf: Buffer): NativeImage {
    const u8 = new Uint8Array(buf.buffer, buf.byteOffset, buf.byteLength)
    const format = detectFormat(u8)
    if (format === 'png') return new NativeImage(decodePNG(u8), buf, 'png')
    return new NativeImage(null, buf, format)
  }

  static fromDataURL(dataURL: string): NativeImage {
    const comma = dataURL.indexOf(',')
    if (comma < 0) return new NativeImage(null, null, 'unknown')
    return NativeImage.fromBuffer(Buffer.from(dataURL.slice(comma + 1), 'base64'))
  }

  static fromPath(path: string): NativeImage {
    try {
      return NativeImage.fromBuffer(fs.readFileSync(path))
    } catch {
      return new NativeImage(null, null, 'unknown')
    }
  }

  isEmpty(): boolean {
    return !this._rgba && !this._fallback
  }

  getSize(): Size {
    if (this._rgba) return { width: this._rgba.width, height: this._rgba.height }
    if (this._fallback) {
      const u8 = new Uint8Array(this._fallback.buffer, this._fallback.byteOffset, this._fallback.byteLength)
      const p = pngSize(u8)
      if (p) return p
      const j = jpegSize(u8)
      if (j) return j
    }
    return { width: 0, height: 0 }
  }

  toPNG(): Buffer {
    if (this._format === 'png' && this._fallback) return this._fallback
    if (!this._rgba) throw new Error('nativeImage.toPNG: imagen no decodificable')
    return encodePNG(this._rgba)
  }

  toJPEG(_quality = 80): Buffer {
    if (this._format === 'jpeg' && this._fallback) return this._fallback
    throw new Error('nativeImage.toJPEG: solo disponible si la fuente ya es JPEG')
  }

  toDataURL(): string {
    if (this._format === 'jpeg' && this._fallback)
      return `data:image/jpeg;base64,${this._fallback.toString('base64')}`
    const png = this._format === 'png' && this._fallback ? this._fallback : this.toPNG()
    return `data:image/png;base64,${png.toString('base64')}`
  }

  resize(options: ResizeOptions): NativeImage {
    if (!this._rgba) throw new Error('nativeImage.resize: imagen no decodificable')
    const size = this.getSize()
    let w = options.width ?? 0
    let h = options.height ?? 0
    if (!w && !h) return this
    if (!w) w = Math.max(1, Math.round((size.width * h) / size.height))
    if (!h) h = Math.max(1, Math.round((size.height * w) / size.width))
    return new NativeImage(resizeBilinear(this._rgba, w, h), null, 'png')
  }

  crop(rect: Rectangle): NativeImage {
    if (!this._rgba) throw new Error('nativeImage.crop: imagen no decodificable')
    const { x, y, width, height } = rect
    const out = new Uint8Array(width * height * 4)
    for (let r = 0; r < height; r++) {
      for (let c = 0; c < width; c++) {
        const so = ((y + r) * this._rgba.width + (x + c)) * 4
        const dofs = (r * width + c) * 4
        out[dofs] = this._rgba.data[so]
        out[dofs + 1] = this._rgba.data[so + 1]
        out[dofs + 2] = this._rgba.data[so + 2]
        out[dofs + 3] = this._rgba.data[so + 3]
      }
    }
    return new NativeImage({ width, height, data: out }, null, 'png')
  }
}

/**
 * `nativeImage` (sin dependencias nativas). Síncrono como Electron para
 * `createFrom*`/`getSize`/`toPNG`/`resize`/`crop`.
 */
export const nativeImage = {
  createFromPath(path: string): NativeImage {
    return NativeImage.fromPath(path)
  },
  createFromBuffer(buf: Buffer): NativeImage {
    return NativeImage.fromBuffer(buf)
  },
  createFromDataURL(dataURL: string): NativeImage {
    return NativeImage.fromDataURL(dataURL)
  },
}
