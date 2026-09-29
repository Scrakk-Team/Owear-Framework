// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/appicon.ts — icono por defecto de la app (compartido).
//
// Módulo sin dependencias para evitar ciclos: `app.setIcon(path)` lo fija y
// `BrowserWindow` lo aplica a cada ventana creada a partir de entonces.

let iconPath: string | undefined

/** Fija el icono por defecto (ruta a PNG/JPEG). */
export function setWindowIcon(path?: string): void {
  iconPath = path
}

/** Icono por defecto actual (o undefined). */
export function windowIcon(): string | undefined {
  return iconPath
}
