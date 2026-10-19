import { createApp, watch } from 'vue'
import { createVuetify } from 'vuetify'
import 'vuetify/styles'
import '@mdi/font/css/materialdesignicons.css'
import App from './App.vue'
import './style.css'
import './ssb-chrome.css'
import './workspace.css'
import { THEMES, isLight } from './theme/themes'
import { native, request } from './native/bridge'
import { currentTheme, themeId, visualVars } from './theme/current'
const vuetify=createVuetify({theme:{defaultTheme:themeId.value,themes:Object.fromEntries(THEMES.map(t=>[t.id,{dark:!isLight(t.bg[2]),colors:{primary:t.primary,secondary:t.secondary,accent:t.accent,success:t.success,error:t.danger,background:t.bg[2],surface:t.panel[1]}}]))}})
watch(currentTheme,t=>{for(const [key,value]of Object.entries(visualVars(t)))document.documentElement.style.setProperty(key,value);vuetify.theme.change(t.id);if(native())void request('theme',t.bg[2]).catch(console.error)},{immediate:true})
createApp(App).use(vuetify).mount('#app')
