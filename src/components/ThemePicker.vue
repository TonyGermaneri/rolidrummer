<script setup lang="ts">
import {computed,watch,onBeforeUnmount} from 'vue'
import {THEME_GROUPS,THEMES,themeGroup,themeById,type Theme} from '../theme/themes'
import {themeId,themePreview} from '../theme/current'
const open=defineModel<boolean>({default:false})
const props=withDefaults(defineProps<{busy?:boolean;inline?:boolean}>(),{busy:false,inline:false})
const emit=defineEmits<{select:[id:string]}>()
const selected=computed(()=>themeById(themeId.value))
const groups=computed(()=>THEME_GROUPS.map(name=>({name,themes:THEMES.filter(t=>themeGroup(t)===name)})))
function preview(t:Theme){themePreview.value=t.id}
function restore(){themePreview.value=null}
function pick(t:Theme){if(props.busy)return;restore();emit('select',t.id);if(!props.inline)open.value=false}
function cycle(step:number){if(props.busy)return;restore();const i=THEMES.findIndex(t=>t.id===themeId.value);emit('select',THEMES[(i+step+THEMES.length)%THEMES.length].id)}
watch(open,value=>{if(!value)restore()})
onBeforeUnmount(restore)
</script>

<template>
  <div class="picker" :role="inline?'group':'dialog'" aria-label="Choose color theme" @pointerdown.stop @keydown.esc="open=false">
    <div class="head">
      <button class="hw-btn icon" title="Previous theme" aria-label="Previous theme" :disabled="busy" @click="cycle(-1)">
        ‹
      </button>
      <div class="current">{{ selected.name }}</div>
      <button class="hw-btn icon" title="Next theme" aria-label="Next theme" :disabled="busy" @click="cycle(1)">
        ›
      </button>
      <span class="count">{{ THEMES.length }} THEMES · HOVER TO PREVIEW</span><button v-if="!inline" class="hw-btn icon" aria-label="Close theme picker" @click="open=false">×</button>
    </div>
    <div class="body" @mouseleave="restore">
      <section v-for="g in groups" :key="g.name">
        <h6>{{ g.name }} <b>{{ g.themes.length }}</b></h6>
        <div class="tiles">
          <button
            v-for="t in g.themes"
            :key="t.id"
            class="tile"
            :data-theme="t.id"
            :aria-pressed="t.id===themeId"
            :disabled="busy"
            :class="{ on: t.id === themeId }"
            :style="{ background: `linear-gradient(180deg, ${t.panel[0]}, ${t.panel[2]})`, color: t.text[0] }"
            @mouseenter="preview(t)"
            @focus="preview(t)"
            @click="pick(t)"
          >
            <span class="sw">
              <i :style="{ background: t.bg[2] }" />
              <i :style="{ background: t.primary }" />
              <i :style="{ background: t.secondary }" />
              <i :style="{ background: t.accent }" />
              <i :style="{ background: t.wood[1] }" />
            </span>
            <span class="nm">{{ t.name }}</span>
          </button>
        </div>
      </section>
    </div>
  </div>
</template>

<style scoped>
.hw-btn.icon{font:20px/20px monospace;min-width:30px}.hw-btn{padding:3px 7px;border:1px solid #000;border-radius:3px;background:linear-gradient(var(--panel-hi),var(--panel-lo));box-shadow:inset 0 1px #ffffff1f,0 2px #0007;color:var(--text-dim);cursor:pointer}.hw-btn:hover{color:var(--c-primary)}button:disabled{opacity:.5;cursor:wait}.hw-btn:focus-visible{outline:2px solid var(--c-secondary)}
@media(max-width:700px){.head{flex-wrap:wrap}.count{font-size:10px!important}.current{min-width:120px!important;font-size:17px!important}}

.picker {
  width: min(880px, calc(100vw - 32px));
  max-height: calc(100vh - 110px);
  display: flex;
  flex-direction: column;
  border-radius: 6px;
  background: linear-gradient(180deg, var(--panel-hi), var(--panel-lo));
  border: 1px solid #000;
  box-shadow: 0 12px 32px rgba(0, 0, 0, 0.7);
}
.head {
  flex: none;
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 10px;
  border-bottom: 1px solid rgba(0, 0, 0, 0.5);
}
.current {
  min-width: 180px;
  padding: 2px 10px;
  border-radius: 3px;
  text-align: center;
  font-family: 'VT323', monospace;
  font-size: 22px;
  color: var(--c-primary);
  background: var(--lcd-bg);
}
.count {
  margin-left: auto;
  font-family: 'VT323', monospace;
  font-size: 14px;
  color: var(--text-mute);
}
.body {
  overflow-y: auto;
  padding: 4px 10px 10px;
  scrollbar-width: thin;
}
h6 {
  margin: 8px 0 4px;
  font-family: 'Orbitron', sans-serif;
  font-size: 9px;
  font-weight: 900;
  letter-spacing: 0.2em;
  color: var(--text-head);
}
h6 b {
  font-weight: 400;
  color: var(--text-mute);
}
.tiles {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(150px, 1fr));
  gap: 5px;
}
.tile {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 4px;
  padding: 5px 7px 4px;
  border-radius: 4px;
  border: 1px solid #000;
  box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.12);
  text-align: left;
}
.tile:hover,
.tile:focus-visible {
  outline: 2px solid var(--c-secondary);
  outline-offset: 1px;
}
.tile.on {
  outline: 2px solid var(--c-primary);
  outline-offset: 1px;
}
.sw {
  display: flex;
  gap: 2px;
}
.sw i {
  width: 16px;
  height: 10px;
  border-radius: 2px;
  box-shadow: 0 0 0 1px rgba(0, 0, 0, 0.5);
}
.nm {
  font-family: 'VT323', monospace;
  font-size: 16px;
  line-height: 1;
  white-space: nowrap;
}
</style>
