# @owear/linux-x64-gnu

Runtime de Owear para **Linux x64 (glibc)**: el binario del kernel nativo, los
módulos stock (`.owm`) y los headers públicos.

No se usa directamente: lo instala `@owear/cli` como dependencia opcional según
tu plataforma. El CLI lo detecta y lo usa en vez de compilar el kernel.

## Contenido

```
bin/owear          binario del kernel (WebKitGTK)
bin/modules/*.so   módulos stock: fs, path, process, screen, net, dialog, …
include/**         headers para `owear-build-native` (tus módulos native/*.cpp)
```

## Requisitos del sistema (Linux)

El binario enlaza con WebKitGTK/GTK3/OpenSSL. En Debian/Ubuntu:

```bash
sudo apt-get install -y libgtk-3-dev libwebkit2gtk-4.1-dev libssl-dev
```

## Uso

Normalmente vía:

```bash
pnpm dlx @owear/cli create mi-app
```

O directamente el kernel (con tus módulos):

```bash
OW_MODULES_DIR="$(dirname $(readlink -f $(which owear)))/modules" owear
```
