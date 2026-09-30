// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// Tests de la firma de artefactos (tools/owear-sign.mjs): construcción del
// comando Authenticode, detección de PE/MSI y firma Ed25519 desprendida.

import { test } from 'node:test'
import assert from 'node:assert/strict'
import * as crypto from 'node:crypto'
import * as fs from 'node:fs'
import * as os from 'node:os'
import * as path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const TOOL = path.resolve(__dirname, '../../cli/tools/owear-sign.mjs')

const sign = await import(TOOL)

function tmpFile(name, bytes) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'owear-sign-'))
  const f = path.join(dir, name)
  fs.writeFileSync(f, bytes)
  return f
}

const PE = () => Buffer.concat([Buffer.from('MZ'), Buffer.alloc(200)])
const ELF = () => Buffer.concat([Buffer.from([0x7f, 0x45, 0x4c, 0x46]), Buffer.alloc(200)])

test('sign: detecta PE y MSI, no confunde ELF', () => {
  assert.equal(sign.isAuthenticodeTarget('/tmp/app.exe'), true)
  assert.equal(sign.isAuthenticodeTarget('/tmp/App.msi'), true)
  assert.equal(sign.isAuthenticodeTarget(tmpFile('a.exe', PE())), true)
  assert.equal(sign.isAuthenticodeTarget(tmpFile('a', ELF())), false)
})

test('sign: comando Authenticode con osslsigncode', () => {
  const inv = sign.buildAuthenticodeInvocation({
    tool: 'osslsigncode',
    file: '/tmp/App.exe',
    pfx: '/tmp/cert.p12',
    password: 'secreto',
    timestamp: 'http://ts.example',
    name: 'Mi App',
    url: 'https://miapp.dev',
  })
  assert.equal(inv.cmd, 'osslsigncode')
  assert.deepEqual(inv.args, [
    'sign', '-pkcs12', '/tmp/cert.p12', '-in', '/tmp/App.exe', '-out', '/tmp/App.exe.signed',
    '-pass', 'secreto', '-n', 'Mi App', '-i', 'https://miapp.dev', '-t', 'http://ts.example',
  ])
  assert.equal(inv.inPlace, true)
})

test('sign: comando Authenticode con signtool', () => {
  const inv = sign.buildAuthenticodeInvocation({
    tool: 'signtool',
    file: 'C:\\App.exe',
    pfx: 'cert.p12',
    password: 'secreto',
    timestamp: 'http://ts.example',
    name: 'Mi App',
  })
  assert.equal(inv.cmd, 'signtool')
  assert.deepEqual(inv.args, [
    'sign', '/f', 'cert.p12', '/fd', 'SHA256', '/d', 'Mi App',
    '/p', 'secreto', '/tr', 'http://ts.example', '/td', 'SHA256', 'C:\\App.exe',
  ])
})

test('sign: Ed25519 desprendida round-trip y detección de manipulación', () => {
  const { publicKey, privateKey } = crypto.generateKeyPairSync('ed25519')
  const keyPath = tmpFile('k.pem', privateKey.export({ format: 'pem', type: 'pkcs8' }))
  const pubB64 = publicKey.export({ format: 'der', type: 'spki' }).toString('base64')
  const file = tmpFile('MiApp', Buffer.from('contenido del binario'))

  sign.signEd25519(file, keyPath)
  assert.ok(fs.existsSync(`${file}.sig`))
  assert.equal(sign.verifyEd25519(file, `${file}.sig`, pubB64), true)
  assert.equal(sign.verifyEd25519(file, `${file}.sig`, publicKey.export({ format: 'pem', type: 'spki' })), true)

  // manipular el binario invalida la firma
  fs.appendFileSync(file, 'x')
  assert.equal(sign.verifyEd25519(file, `${file}.sig`, pubB64), false)
})

test('sign: CLI --dry-run imprime el comando Authenticode', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'owear-sign-cli-'))
  const exe = path.join(dir, 'App.exe')
  fs.writeFileSync(exe, PE())
  const pfx = path.join(dir, 'cert.p12')
  fs.writeFileSync(pfx, 'no-es-un-pfx-real-solo-dry-run')

  const r = spawnSync(
    'node',
    [TOOL, '--file', exe, '--pfx', pfx, '--name', 'Mi App', '--timestamp', 'http://ts.example', '--dry-run'],
    { encoding: 'utf8' },
  )
  assert.equal(r.status, 0, r.stderr)
  const out = JSON.parse(r.stdout.trim())
  assert.ok(['osslsigncode', 'signtool'].includes(out.authenticode.cmd))
  assert.ok(out.authenticode.args.includes(pfx))

  fs.rmSync(dir, { recursive: true, force: true })
})

test('sign: CLI firma en Ed25519 y crea <file>.sig', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'owear-sign-cli2-'))
  const { publicKey, privateKey } = crypto.generateKeyPairSync('ed25519')
  const key = path.join(dir, 'k.pem')
  fs.writeFileSync(key, privateKey.export({ format: 'pem', type: 'pkcs8' }))
  const bin = path.join(dir, 'MiApp')
  fs.writeFileSync(bin, 'binario de prueba')

  const r = spawnSync('node', [TOOL, '--file', bin, '--ed25519-key', key], { encoding: 'utf8' })
  assert.equal(r.status, 0, r.stderr)
  assert.ok(fs.existsSync(`${bin}.sig`), 'debe crear el .sig')
  assert.equal(
    crypto.verify(null, fs.readFileSync(bin), publicKey, Buffer.from(fs.readFileSync(`${bin}.sig`, 'utf8'), 'base64')),
    true,
  )

  fs.rmSync(dir, { recursive: true, force: true })
})

test('sign: Authenticode real (skip si no hay osslsigncode/signtool)', (t) => {
  const tool = sign.hasTool('osslsigncode') ? 'osslsigncode' : sign.hasTool('signtool') ? 'signtool' : null
  if (!tool) return t.skip('sin osslsigncode/signtool en el PATH')
  // Sin certificado no probamos la firma real aquí (requiere PKCS#12).
  return t.skip(`herramienta ${tool} presente pero sin certificado de prueba`)
})
