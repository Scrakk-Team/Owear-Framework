// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/dialog.ts — diálogos nativos (módulo `dialog`).
import { channel, invokeNative } from './channel.js'

// ── dialog ──────────────────────────────────────────────────────────────────

export interface FileFilter {
  name: string
  extensions: string[]
}

export interface OpenDialogOptions {
  title?: string
  defaultPath?: string
  buttonLabel?: string
  filters?: FileFilter[]
  properties?: Array<
    'openFile' | 'openDirectory' | 'multiSelections' | 'showHiddenFiles' | 'createDirectory'
  >
}

export interface SaveDialogOptions {
  title?: string
  defaultPath?: string
  buttonLabel?: string
  filters?: FileFilter[]
}

export interface MessageBoxOptions {
  type?: 'none' | 'info' | 'error' | 'question' | 'warning'
  title?: string
  message: string
  detail?: string
  buttons?: string[]
  defaultId?: number
  cancelId?: number
  checkboxLabel?: string
}

/**
 * Diálogos nativos (módulo `dialog`).
 *   const { canceled, filePaths } = await dialog.showOpenDialog({ properties: ['openFile','multiSelections'] })
 *   const { filePath } = await dialog.showSaveDialog({ defaultPath: '/tmp/x.txt' })
 *   const { response } = await dialog.showMessageBox({ message: '¿Seguir?', buttons: ['Sí','No'] })
 */
export const dialog = {
  showOpenDialog(options: OpenDialogOptions = {}): Promise<{ canceled: boolean; filePaths: string[] }> {
    return invokeNative('dialog', 'showOpenDialog', options)
  },
  showSaveDialog(options: SaveDialogOptions = {}): Promise<{ canceled: boolean; filePath: string }> {
    return invokeNative('dialog', 'showSaveDialog', options)
  },
  showMessageBox(options: MessageBoxOptions): Promise<{ response: number; checkboxChecked: boolean }> {
    return invokeNative('dialog', 'showMessageBox', options)
  },
}
