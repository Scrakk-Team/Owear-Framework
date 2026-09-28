import { app, BrowserWindow } from '/home/julian/Documentos/Proyectos/Owear/packages/core/dist/index.js'

app.whenReady().then(() => {
  const win = new BrowserWindow({
    title: 'Owear bench',
    width: 900,
    height: 700,
    show: true,
    url: process.env.OW_BENCH_URL || 'app://index.html',
  })
  app.handle('bench.done', () => {
    setTimeout(() => app.quit(0), 150)
    return null
  })
})
