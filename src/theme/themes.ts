/** Colour themes: eras and genres, games, computers, movies, TV. Applied as CSS variables + a matching Vuetify theme. */
import { COMPUTER_THEMES, ERA_THEMES, GAME_THEMES, MOVIE_THEMES, TV_THEMES } from './themePacks'

export const THEME_GROUPS = ['ERAS', 'GAMES', 'COMPUTERS', 'MOVIES', 'TV'] as const
export type ThemeGroup = (typeof THEME_GROUPS)[number]

export interface Theme {
  id: string
  name: string
  /** picker section (the hand-written originals below are ERAS) */
  group?: ThemeGroup
  primary: string // main LED / readouts
  secondary: string
  accent: string
  success: string
  danger: string
  panel: [string, string, string] // hi → lo
  bg: [string, string, string] // hi, lo, base
  wood: [string, string, string] // header end caps: hi, mid, lo
  text: [string, string, string, string, string] // body, headings, labels, muted, faint
  lcd: string
  lcdIdle: string
  logo: [string, string, string]
  padHues: number[]
  padSat: number // %
  padLight: number // %
}

const ORIGINALS: Theme[] = [
  {
    id: 'console85',
    name: "CONSOLE '85",
    primary: '#ffb000',
    secondary: '#27e0ff',
    accent: '#ff3d7f',
    success: '#33ff66',
    danger: '#ff3b2f',
    panel: ['#3a3940', '#2a292f', '#222126'],
    bg: ['#1b1a1f', '#121114', '#0e0d10'],
    wood: ['#6d4527', '#5b3a22', '#3e2615'],
    text: ['#e8e2d4', '#e8dcc0', '#b9b4a8', '#8a8579', '#57534b'],
    lcd: '#0b0703',
    lcdIdle: '#5a3d00',
    logo: ['#ffe07a', '#ff8a00', '#ff2d6f'],
    padHues: [330, 188, 42, 135, 268, 18, 300, 205],
    padSat: 85,
    padLight: 52,
  },
  {
    id: 'diner55',
    name: "DINER '55",
    primary: '#ff3b4f',
    secondary: '#3fbfa4',
    accent: '#ff7eb3',
    success: '#3f9ad8',
    danger: '#e0182c',
    panel: ['#e8eaee', '#cfd2d8', '#b3b7bf'],
    bg: ['#f3ead3', '#e4d5b2', '#d6c49c'],
    wood: ['#e0344a', '#b8202f', '#8a1623'],
    text: ['#1d1d20', '#1a1a1d', '#3e3e45', '#5e5e66', '#8a8272'],
    lcd: '#10151a',
    lcdIdle: '#4a1c22',
    logo: ['#ff9ec4', '#ff3b4f', '#8a1623'],
    padHues: [352, 165, 200, 48, 330, 15],
    padSat: 72,
    padLight: 60,
  },
  {
    id: 'surf62',
    name: "SURF '62",
    primary: '#ffcf3f',
    secondary: '#3fd6e0',
    accent: '#ff7a5c',
    success: '#9be564',
    danger: '#ff4040',
    panel: ['#2f5561', '#224550', '#183640'],
    bg: ['#123038', '#0b2229', '#07181d'],
    wood: ['#c98f55', '#a8733f', '#80552b'],
    text: ['#f2efe4', '#fff4d6', '#b8d3d6', '#8aa7ab', '#5b777b'],
    lcd: '#061417',
    lcdIdle: '#4a3d10',
    logo: ['#fff4a8', '#ffcf3f', '#ff7a5c'],
    padHues: [190, 45, 12, 170, 95, 205],
    padSat: 70,
    padLight: 50,
  },
  {
    id: 'psych67',
    name: "PSYCH '67",
    primary: '#ff8c1a',
    secondary: '#c2ff3d',
    accent: '#ff2fd0',
    success: '#9b5cff',
    danger: '#ff3b3b',
    panel: ['#4a2a5e', '#3a1f4c', '#2b1639'],
    bg: ['#2a1236', '#1d0b27', '#14071c'],
    wood: ['#ff8c1a', '#d9690b', '#a84d05'],
    text: ['#fbe9ff', '#ffd9f5', '#d3b3e0', '#a283b3', '#6e5480'],
    lcd: '#12061a',
    lcdIdle: '#4a2a08',
    logo: ['#c2ff3d', '#ff8c1a', '#ff2fd0'],
    padHues: [25, 300, 80, 270, 180, 330],
    padSat: 90,
    padLight: 52,
  },
  {
    id: 'funk74',
    name: "FUNK '74",
    primary: '#ffae42',
    secondary: '#d8c25a',
    accent: '#e2583e',
    success: '#9bb04f',
    danger: '#d7372b',
    panel: ['#5a4431', '#46341f', '#352716'],
    bg: ['#2e2217', '#221910', '#18110a'],
    wood: ['#9c6b3d', '#7a522c', '#58391c'],
    text: ['#f4e6cf', '#f7dcae', '#cdb793', '#a08a68', '#6e5c43'],
    lcd: '#140c05',
    lcdIdle: '#4a300a',
    logo: ['#f7dcae', '#ffae42', '#e2583e'],
    padHues: [28, 45, 75, 15, 95, 35],
    padSat: 60,
    padLight: 45,
  },
  {
    id: 'disco77',
    name: "DISCO '77",
    primary: '#ffd23f',
    secondary: '#e6e6ff',
    accent: '#ff2a9d',
    success: '#b388ff',
    danger: '#ff3b5c',
    panel: ['#352a40', '#261d30', '#1a1322'],
    bg: ['#1c1224', '#120b18', '#0b0610'],
    wood: ['#d8d8e8', '#a8a8bd', '#77778c'],
    text: ['#fff5e0', '#ffe7a3', '#cfc3dd', '#9f92ae', '#6c617a'],
    lcd: '#0e0714',
    lcdIdle: '#4d3d0f',
    logo: ['#fff4b0', '#ffd23f', '#ff2a9d'],
    padHues: [45, 300, 275, 190, 330, 210],
    padSat: 85,
    padLight: 55,
  },
  {
    id: 'roots78',
    name: "ROOTS '78",
    primary: '#ffcf2e',
    secondary: '#3ecf5a',
    accent: '#e8322f',
    success: '#ffe9a8',
    danger: '#e8322f',
    panel: ['#2e3b28', '#222d1d', '#172014'],
    bg: ['#161f13', '#10170d', '#0a0f08'],
    wood: ['#e8322f', '#ffcf2e', '#2c9b45'],
    text: ['#f3efd9', '#fff2c4', '#c9c7a8', '#9a987c', '#66654f'],
    lcd: '#0a1008',
    lcdIdle: '#4a3f0e',
    logo: ['#2c9b45', '#ffcf2e', '#e8322f'],
    padHues: [0, 48, 130, 48, 0, 130],
    padSat: 80,
    padLight: 45,
  },
  {
    id: 'outrun86',
    name: "OUTRUN '86",
    primary: '#ff2e97',
    secondary: '#00f0ff',
    accent: '#b026ff',
    success: '#ffe600',
    danger: '#ff3355',
    panel: ['#2e2150', '#21173d', '#16102b'],
    bg: ['#1a1033', '#100a22', '#090616'],
    wood: ['#6a2bd1', '#4b1d99', '#2f1166'],
    text: ['#f3e8ff', '#ffd6f0', '#c4b0e6', '#9480b8', '#62548a'],
    lcd: '#0c0619',
    lcdIdle: '#4d0e32',
    logo: ['#ffe600', '#ff2e97', '#b026ff'],
    padHues: [320, 185, 275, 50, 200, 300],
    padSat: 95,
    padLight: 55,
  },
  {
    id: 'metal88',
    name: "METAL '88",
    primary: '#ff3322',
    secondary: '#c0c6cc',
    accent: '#ff7a00',
    success: '#e8e8e8',
    danger: '#ff1a1a',
    panel: ['#343434', '#242424', '#171717'],
    bg: ['#141414', '#0c0c0c', '#070707'],
    wood: ['#4a4a4a', '#2e2e2e', '#1a1a1a'],
    text: ['#eeeeee', '#ffffff', '#b5b5b5', '#8a8a8a', '#555555'],
    lcd: '#100505',
    lcdIdle: '#4a0f0a',
    logo: ['#ffffff', '#c0c6cc', '#ff3322'],
    padHues: [0, 20, 350, 210, 30, 0],
    padSat: 80,
    padLight: 40,
  },
  {
    id: 'bit89',
    name: "8-BIT '89",
    primary: '#c4e34a',
    secondary: '#9bbc0f',
    accent: '#e0f8a0',
    success: '#8bac0f',
    danger: '#ff5040',
    panel: ['#4a5a20', '#3a4818', '#2b3610'],
    bg: ['#2a3510', '#1f280b', '#151c07'],
    wood: ['#8b9aa8', '#6e7c89', '#525e6a'],
    text: ['#e0f8d0', '#e8ffc0', '#b8d090', '#8aa060', '#5a6e3a'],
    lcd: '#0f380f',
    lcdIdle: '#306230',
    logo: ['#e0f8d0', '#9bbc0f', '#306230'],
    padHues: [80, 90, 100, 70, 110, 85],
    padSat: 55,
    padLight: 40,
  },
  {
    id: 'grunge93',
    name: "GRUNGE '93",
    primary: '#d9c27a',
    secondary: '#8fa37a',
    accent: '#c0463d',
    success: '#7a9bb0',
    danger: '#c0463d',
    panel: ['#46443b', '#35342d', '#26251f'],
    bg: ['#22211c', '#191814', '#11100d'],
    wood: ['#7a2e2a', '#5c211e', '#3f1614'],
    text: ['#e6e0cf', '#efe6c8', '#b3ad99', '#88836f', '#5d5a4c'],
    lcd: '#0f0e0a',
    lcdIdle: '#3f3820',
    logo: ['#efe6c8', '#d9c27a', '#c0463d'],
    padHues: [5, 80, 45, 200, 25, 140],
    padSat: 38,
    padLight: 42,
  },
  {
    id: 'vapor95',
    name: "VAPOR '95",
    primary: '#ff9ce6',
    secondary: '#7df9ff',
    accent: '#b19cff',
    success: '#fffb96',
    danger: '#ff6b8b',
    panel: ['#4a3a66', '#3a2d52', '#2c2240'],
    bg: ['#2a2140', '#1e172f', '#140f21'],
    wood: ['#7df9ff', '#5ac8d8', '#3a93a3'],
    text: ['#fff0fb', '#ffe0f7', '#d2c3ea', '#a695c2', '#74679a'],
    lcd: '#140c1f',
    lcdIdle: '#4d2a47',
    logo: ['#fffb96', '#ff9ce6', '#7df9ff'],
    padHues: [320, 180, 260, 55, 200, 290],
    padSat: 85,
    padLight: 70,
  },
  {
    id: 'noir49',
    name: "NOIR '49",
    primary: '#d4a84b',
    secondary: '#8fb3d6',
    accent: '#c7514a',
    success: '#e8dcc0',
    danger: '#c7514a',
    panel: ['#343a45', '#262b34', '#1b1f26'],
    bg: ['#171a20', '#101318', '#0a0c10'],
    wood: ['#8a6a3a', '#6b512a', '#4a371b'],
    text: ['#ece6d8', '#f2e3bf', '#b7b3aa', '#8a877f', '#5d5b55'],
    lcd: '#0b0d11',
    lcdIdle: '#3f3218',
    logo: ['#f2e3bf', '#d4a84b', '#8a6a3a'],
    padHues: [210, 40, 5, 190, 30, 260],
    padSat: 40,
    padLight: 45,
  },
]

/** year from the name ("OUTRUN '86" → 1986) */
const year = (t: Theme) => {
  const y = parseInt(/'(\d\d)/.exec(t.name)?.[1] ?? '80', 10)
  return y < 30 ? 2000 + y : 1900 + y
}
export const themeGroup = (t: Theme): ThemeGroup => t.group ?? 'ERAS'

/** every theme, by group then year (the logo's ◀ ▶ steps through them in this order); console85 stays the default */
export const THEMES: Theme[] = [...ORIGINALS, ...ERA_THEMES, ...GAME_THEMES, ...COMPUTER_THEMES, ...MOVIE_THEMES, ...TV_THEMES]
  .map((t, i) => ({ t, i }))
  .sort((a, b) => THEME_GROUPS.indexOf(themeGroup(a.t)) - THEME_GROUPS.indexOf(themeGroup(b.t)) || year(a.t) - year(b.t) || a.i - b.i)
  .map(({ t }) => t)

export const themeById = (id: string) => THEMES.find((t) => t.id === id) ?? ORIGINALS[0]

/** a light colour (weighted sRGB brightness above 0.55): light-panelled themes need light ink on dark insets */
export function isLight(hex: string) {
  const [r, g, b] = [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16) / 255)
  return 0.2126 * r + 0.7152 * g + 0.0722 * b > 0.55
}

/** CSS custom properties for a theme (set on :root so dialogs and menus get them too). */
export function themeVars(t: Theme): Record<string, string> {
  return {
    '--c-primary': t.primary,
    '--c-secondary': t.secondary,
    '--c-accent': t.accent,
    '--c-success': t.success,
    '--c-danger': t.danger,
    '--panel-hi': t.panel[0],
    '--panel-mid': t.panel[1],
    '--panel-lo': t.panel[2],
    '--bg-hi': t.bg[0],
    '--bg-lo': t.bg[1],
    '--bg': t.bg[2],
    '--wood-hi': t.wood[0],
    '--wood': t.wood[1],
    '--wood-lo': t.wood[2],
    '--text': t.text[0],
    '--text-head': t.text[1],
    '--text-dim': t.text[2],
    '--text-mute': t.text[3],
    '--text-faint': t.text[4],
    '--lcd-bg': t.lcd,
    '--lcd-idle': t.lcdIdle,
    '--logo-1': t.logo[0],
    '--logo-2': t.logo[1],
    '--logo-3': t.logo[2],
    // text on the dark insets (chips, tabs): light themes' own grey text would vanish on them
    '--ink-dim': isLight(t.panel[1]) ? '#c8c8c8' : t.text[2],
    '--ink-mute': isLight(t.panel[1]) ? '#949494' : t.text[3],
    '--pad-sat': `${t.padSat}%`,
    '--pad-light': `${t.padLight}%`,
  }
}

export function applyThemeVars(t: Theme) {
  const root = document.documentElement.style
  for (const [k, v] of Object.entries(themeVars(t))) root.setProperty(k, v)
}
