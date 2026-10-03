// Render the real Vue playing surface as a standalone, inspectable SVG.
import { createServer } from 'vite'
import { createSSRApp } from 'vue'
import { renderToString } from '@vue/server-renderer'
import { readFile, writeFile } from 'node:fs/promises'
globalThis.window = { addEventListener() {}, removeEventListener() {} }
const server = await createServer({ server: { middlewareMode: true, hmr: false, ws: false }, appType: 'custom' })
let status = 0
try {
 const { default: Seaboard } = await server.ssrLoadModule('/src/components/Seaboard.vue')
 const {visualVars,currentTheme}=await server.ssrLoadModule('/src/theme/current.ts')
 const variables=visualVars(currentTheme.value)
 const vars=Object.entries(variables).map(([k,v])=>`${k}:${v}`).join(';')
 const html = await renderToString(createSSRApp(Seaboard, { base: 36 }))
 const raw = html.slice(html.indexOf('<svg'), html.indexOf('</svg>') + 6).replace(/\sdata-v-[\da-f]+(?:="")?/g, '')
 const source=await readFile('src/components/Seaboard.vue','utf8')
 const css=`<style>svg{${vars}}${source.match(/<style scoped>([\s\S]*?)<\/style>/)[1]}</style>`
 // Resolve variables for standalone SVG consumers such as librsvg.
 const svg = raw.replace('<svg ', '<svg xmlns="http://www.w3.org/2000/svg" width="1410" height="420" ').replace('</svg>', css + '</svg>').replace(/var\((--[\w-]+)(?:,([^)]*))?\)/g,(_,name,fallback)=>variables[name]??fallback??'inherit')
 await writeFile('docs/seaboard-preview.svg', svg)
 console.log('Exported docs/seaboard-preview.svg from Seaboard.vue')
} catch (error) { console.error(error); status = 1 }
finally { await server.close(); process.exit(status) }
