<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# @owear/win32-x64

Runtime de Owear para **Windows x64**: el binario del kernel nativo, los módulos
stock (`.owm`) y los headers públicos.

No se usa directamente: lo instala `@owear/cli` como dependencia opcional según
tu plataforma. El CLI lo detecta y lo usa **en vez de compilar** el kernel.

## Contenido

```
bin/owear.exe      binario del kernel (WebView2)
bin/modules/*.dll  módulos stock: fs, path, process, screen, net, dialog, …
include/**         headers para `owear-build-native` (tus módulos native/*.cpp)
```

## Requisitos del sistema (Windows)

- **WebView2 Runtime**: viene preinstalado en Windows 11 y en la mayoría de
  Windows 10 con Edge actualizado. Si falta:
  <https://developer.microsoft.com/microsoft-edge/webview2/>
- Nada más. **No** necesitas Visual Studio, CMake, vcpkg, OpenSSL ni el runtime
  VC++ (todo va incluido en este paquete).

## Uso

```powershell
npm i -g @owear/cli
ow create mi-app
cd mi-app
npm install
npm run dev
```
