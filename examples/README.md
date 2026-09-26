# Ejemplos de Owear

Apps completas y ejecutables que muestran el modelo de Owear en la práctica.

| Ejemplo | Qué demuestra |
|---|---|
| [`starter`](./starter) | **El starter profesional**: UI real, titlebar propia y demos de los módulos nativos (`dialog`, `fs`, `notification`, `clipboard`, `shell`). Sin C++. |
| [`cursor-xray`](./cursor-xray) | **Cómo se usan los módulos nativos bien**: el cursor global y los monitores los da el módulo stock `screen` (nada de C++); para lo que el stock no trae (hostname/CPUs/RAM) hay un `.owm` propio. Todo directo WebView→kernel. |
| [`snake`](./snake) | Que el **IPC no hace falta en el camino caliente**: el juego corre entero en el renderer (canvas, 60 fps, cero mensajes por frame) y persiste el récord con el módulo stock `fs`. |

## Ejecutar un ejemplo

Desde la raíz del monorepo:

```bash
pnpm install

cd examples/<nombre>
pnpm dev
```

`ow dev` compila el kernel (si falta), el sidecar Node, los módulos `native/*.cpp`
a `.owm`, y levanta Vite + el WebView del sistema.

> Requiere **Linux** hoy: es la única plataforma con el kernel verificado E2E.
