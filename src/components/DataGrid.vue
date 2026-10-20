<script setup>
/**
 * A list of any length, drawn on a canvas.
 *
 * The catalogue lists used to page: ten rows and a pager, because ten
 * thousand `<v-list-item>`s is a scroll of jank and three quarters of a
 * million is a dead tab. Paging is what a list does when it cannot hold what
 * it is showing. This can: canvas-datagrid draws the rows that are on screen
 * and no others, so the array behind it can be as long as the catalogue is
 * and scrolling costs the same at row 700,000 as at row 7.
 *
 * What it gives back that paging took away is the thing a musician actually
 * does with a catalogue -- run down it. There is no page thirty-four to find
 * your way to; the wheel goes where you were going.
 *
 * Styling is not CSS. The grid paints into a canvas, so every colour has to
 * be handed over as a value -- which means the theme has to be read rather
 * than inherited, and re-read when it changes. @see core/themes.js
 */
import { onBeforeUnmount, onMounted, ref, shallowRef, watch, nextTick } from 'vue'
import canvasDatagrid from 'canvas-datagrid'
import { currentTheme } from '../theme/current'

const props = defineProps({
  /** The rows. May grow while it is on screen. @see store.js streamDrumRows */
  rows: { type: Array, default: () => [] },
  /** `[{ name, title, width }]`, in the order they should appear. */
  columns: { type: Array, default: () => [] },
  /** Which row is chosen, so the grid can mark it. */
  chosen: { type: Object, default: null },
  /** Matched against `chosen` to find the row again. */
  keyed: { type: String, default: 'id' },
  viewState: { type: Object, default: null },
})

const emit = defineEmits(['pick', 'use', 'key', 'cell', 'context', 'drag', 'view', 'interaction'])

const box = ref(null)
/** Shallow: the grid owns a canvas and its own event plumbing, and making
    any of that reactive would proxy a great deal of nothing. */
const grid = shallowRef(null)
let sizeObserver, sizeFrame = 0, disposed = false
function fit() {
  const one = grid.value, parent = box.value
  if (!one || !parent || disposed) return
  const width = parent.clientWidth, height = parent.clientHeight
  if (width <= 0 || height <= 0) return
  // canvas-datagrid can cache its default 300×150 viewport when mounted in a
  // flex panel before layout. Give its own style API the final pixel extent.
  one.style.width = `${width}px`; one.style.height = `${height}px`
  one.resize(true)
}
function queueFit() {
  if (disposed || sizeFrame) return
  sizeFrame = requestAnimationFrame(() => { sizeFrame = 0; fit() })
}

/*
 * A canvas takes a font string and falls back silently.
 *
 * The theme's own family list is quoted -- `"IBM Plex Mono", "SF Mono",
 * ui-monospace, monospace` -- and a quoted family inside a canvas font
 * shorthand is not parsed by every engine. When it fails the canvas does not
 * complain; it draws in its default, which is a serif, which is how a
 * monospace theme came to have a serif grid in it. Unquoted generic names
 * only, so there is nothing to misparse and the last one always resolves.
 */
const FACE = 'ui-monospace, SFMono-Regular, Menlo, Consolas, monospace'

/**
 * The theme, as the grid takes it.
 *
 * Every colour the component has, not the ones that looked like they
 * mattered. The first version set thirty of the two hundred and seventeen
 * keys and left the rest at their defaults, which are a light spreadsheet's
 * -- `rgba(202,202,202)` grid border, `rgba(172,172,172)` header border,
 * `rgba(255,255,255)` selection handle. On a dark theme those are the bright
 * white outline that was reported around the headers, and black default text
 * on the hover and active states was the same fault the other way up.
 *
 * So the list below is exhaustive: every key from the component's own
 * defaults that carries a colour, each derived from the theme. The only ones
 * left alone are the `debug*` set, which are never drawn.
 */
function dress() {
  const one = grid.value
  if (!one) return
  const t = currentTheme.value
  const theme = { bg:t.bg[1], fg:t.text[0], accent:t.primary, dim:t.text[3], error:t.danger }
  const ink = theme.fg
  const dim = theme.dim
  // Everything is mixed against the ground, because everything is opaque.
  const ground = theme.bg
  const accent = theme.accent

  /*
   * Five tones and two inks, so nothing is a number chosen on the spot.
   *
   * `line` is what a border is: far enough off the ground to separate two
   * cells and nowhere near far enough to be a feature. `edge` is the one
   * step up from it for the outside of the grid and the header, which are
   * real edges rather than rules between rows. Anything brighter than
   * `edge` on this surface reads as a highlight, and a table has nothing
   * to highlight with a line.
   */
  const line = mix(dim, ground, 0.3)
  const edge = mix(dim, ground, 0.5)
  const raised = mix(ink, ground, 0.06)
  const sunk = mix(ink, ground, 0.04)
  const faint = mix(dim, ground, 0.9)

  Object.assign(one.style, {
    /*
     * Fill the box it is in, rather than grow to fit the data.
     *
     * `auto` is the default and it means "be as tall as the rows need",
     * which for a thousand rows is sixteen thousand pixels -- the largest
     * canvas a browser will paint. The grid then has nothing to scroll,
     * because it is already showing everything, and the wheel does nothing.
     * Measured: a 1344x16384 grid over 13,542px of content.
     */
    height: '100%',
    width: '100%',

    /* ---- the grid itself ------------------------------------------- */
    gridBackgroundColor: ground,
    gridBorderColor: edge,

    /* ---- cells ------------------------------------------------------ */
    cellBackgroundColor: ground,
    cellColor: ink,
    cellFont: `12px ${FACE}`,
    cellPaddingLeft: 10,
    cellPaddingRight: 10,
    cellHeight: 26,
    cellBorderColor: line,
    cellBorderWidth: 1,
    cellHoverBackgroundColor: mix(accent, ground, 0.12),
    cellHoverColor: ink,
    cellSelectedBackgroundColor: mix(accent, ground, 0.28),
    cellSelectedColor: ink,

    /* ---- the one cell the keyboard is on ---------------------------- */
    activeCellBackgroundColor: mix(accent, ground, 0.34),
    activeCellColor: ink,
    activeCellFont: `12px ${FACE}`,
    activeCellBorderColor: accent,
    activeCellOverlayBorderColor: accent,
    activeCellHoverBackgroundColor: mix(accent, ground, 0.38),
    activeCellHoverColor: ink,
    activeCellSelectedBackgroundColor: mix(accent, ground, 0.38),
    activeCellSelectedColor: ink,

    /*
     * ---- the column headers ------------------------------------------
     *
     * Where the white outline was. Four keys draw a line up here -- the
     * header, the cap past the last column, the corner over the row
     * numbers, and the grid's own edge -- and three of them were still at
     * the component's light-grey defaults.
     */
    columnHeaderCellBackgroundColor: raised,
    columnHeaderCellColor: mix(ink, ground, 0.72),
    columnHeaderCellFont: `11px ${FACE}`,
    columnHeaderCellBorderColor: edge,
    columnHeaderCellHeight: 26,
    columnHeaderCellHoverBackgroundColor: mix(accent, ground, 0.16),
    columnHeaderCellHoverColor: ink,
    columnHeaderCellCapBackgroundColor: raised,
    columnHeaderCellCapBorderColor: edge,
    activeColumnHeaderCellBackgroundColor: mix(accent, ground, 0.22),
    activeColumnHeaderCellColor: ink,
    // Which way it is sorted, drawn as a little triangle in the header.
    columnHeaderOrderByArrowColor: mix(ink, ground, 0.55),
    columnHeaderOrderByArrowBorderColor: edge,

    /* ---- the row numbers down the side ------------------------------ */
    // They say how far into three quarters of a million you are, which is
    // the thing a pager used to say.
    rowHeaderCellBackgroundColor: sunk,
    rowHeaderCellColor: faint,
    rowHeaderCellFont: `10px ${FACE}`,
    rowHeaderCellBorderColor: mix(dim, ground, 0.36),
    rowHeaderCellHoverBackgroundColor: mix(accent, ground, 0.14),
    rowHeaderCellHoverColor: ink,
    rowHeaderCellSelectedBackgroundColor: mix(accent, ground, 0.22),
    rowHeaderCellSelectedColor: ink,
    rowHeaderCellRowNumberGapColor: line,
    activeRowHeaderCellBackgroundColor: mix(accent, ground, 0.2),
    activeRowHeaderCellColor: ink,
    cornerCellBackgroundColor: raised,
    cornerCellBorderColor: edge,

    /* ---- the scroll bars -------------------------------------------- */
    scrollBarBackgroundColor: ground,
    scrollBarBoxColor: mix(dim, ground, 0.6),
    scrollBarBoxBorderColor: mix(dim, ground, 0.6),
    scrollBarActiveColor: mix(ink, ground, 0.5),
    scrollBarBorderColor: line,
    scrollBarCornerBackgroundColor: ground,
    scrollBarCornerBorderColor: line,
    scrollBarWidth: 9,
    scrollBarBoxMargin: 2,
    scrollBarBoxMinSize: 24,

    /*
     * ---- everything else that can draw ------------------------------
     *
     * Dragging a column, resizing one, the marks a selection leaves
     * behind. None of them is on screen often and every one of them was a
     * bright default when it was: the selection handle is pure white, the
     * overlays are Google blue, the frozen marker is near-white grey.
     */
    selectionOverlayBorderColor: accent,
    selectionHandleColor: accent,
    selectionHandleBorderColor: mix(accent, ground, 0.5),
    fillOverlayBorderColor: mix(dim, ground, 0.7),
    moveOverlayBorderColor: accent,
    reorderMarkerBackgroundColor: mix(ink, ground, 0.08),
    reorderMarkerBorderColor: line,
    reorderMarkerIndexBorderColor: accent,
    resizeMarkerColor: mix(accent, ground, 0.6),
    frozenMarkerColor: line,
    frozenMarkerBorderColor: line,
    frozenMarkerHeaderColor: edge,
    frozenMarkerHoverColor: mix(accent, ground, 0.6),
    frozenMarkerHoverBorderColor: mix(accent, ground, 0.6),
    frozenMarkerActiveColor: mix(accent, ground, 0.3),
    frozenMarkerActiveBorderColor: mix(accent, ground, 0.5),
    frozenMarkerActiveHeaderColor: mix(accent, ground, 0.7),
    groupingAreaBackgroundColor: sunk,
    groupIndicatorColor: faint,
    groupIndicatorBackgroundColor: raised,
    unhideIndicatorColor: ink,
    unhideIndicatorBackgroundColor: raised,
    unhideIndicatorBorderColor: edge,
    treeArrowColor: mix(ink, ground, 0.55),
    treeArrowBorderColor: edge,
    cellTreeIconLineColor: mix(ink, ground, 0.7),
    cellTreeIconBorderColor: edge,
    cellTreeIconFillColor: raised,
    cellTreeIconHoverFillColor: mix(accent, ground, 0.2),

    /*
     * ---- the parts that are HTML, not canvas ------------------------
     *
     * The filter button, the context menu and the editor are real
     * elements the component appends to the page, so these take CSS
     * strings rather than colours. They are styled here for the same
     * reason as the rest: a white menu over a dark grid is the same bug
     * as a white line over one.
     */
    editCellBackgroundColor: ground,
    editCellColor: ink,
    editCellBorder: `solid 1px ${accent}`,
    editCellFontFamily: FACE,
    buttonBackgroundColor: raised,
    buttonBorderColor: edge,
    buttonHoverBackgroundColor: mix(accent, ground, 0.16),
    buttonActiveBackgroundColor: mix(accent, ground, 0.24),
    buttonActiveBorderColor: accent,
    buttonArrowColor: mix(ink, ground, 0.7),
    filterButtonBackgroundColor: raised,
    filterButtonBorderColor: edge,
    filterButtonHoverBackgroundColor: mix(accent, ground, 0.16),
    filterButtonActiveBackgroundColor: mix(accent, ground, 0.24),
    filterButtonArrowColor: mix(ink, ground, 0.7),
    filterButtonArrowBorderColor: edge,
    contextMenuBackground: raised,
    contextMenuColor: ink,
    contextMenuBorder: `solid 1px ${edge}`,
    contextMenuArrowColor: mix(ink, ground, 0.7),
    childContextMenuArrowColor: mix(ink, ground, 0.7),
    contextMenuHoverBackground: mix(accent, ground, 0.3),
    contextMenuHoverColor: ink,
    contextMenuFontFamily: FACE,
    contextFilterInputBackground: ground,
    contextFilterInputColor: ink,
    contextFilterInputBorder: `solid 1px ${edge}`,
    contextFilterInputFontFamily: FACE,
    contextFilterButtonBorder: `solid 1px ${edge}`,
    contextFilterInvalidRegExpBackground: mix(theme.error, ground, 0.6),
    contextFilterInvalidRegExpColor: ink,
    contextMenuFilterInvalidExpresion: mix(theme.error, ground, 0.4),
    mobileEditFontFamily: FACE,
  })
  queueFit()
}

/** `#rgb` or `#rrggbb` to three numbers, or mid grey. */
function read(hex) {
  const clean = String(hex || '').trim().replace('#', '')
  const full = clean.length === 3 ? clean.split('').map((c) => c + c).join('') : clean
  const n = Number.parseInt(full, 16)
  if (!Number.isFinite(n) || full.length !== 6) return [128, 128, 128]
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255]
}

/**
 * Two colours mixed, opaque.
 *
 * Every colour handed to the grid is solid. A translucent one has to be
 * composited, and the grid composites it against whatever happened to be in
 * the canvas already -- the previous frame, not the surface behind it -- so
 * a hovered row over a scrolled one comes out a different colour than the
 * same row standing still. Mixing here means the value is the value.
 */
function mix(over, under, amount) {
  const a = read(over)
  const b = read(under)
  const t = Math.max(0, Math.min(1, amount))
  return `rgb(${[0, 1, 2].map((i) => Math.round(b[i] + (a[i] - b[i]) * t)).join(',')})`
}

/** Which row of the grid holds the chosen thing, or -1. */
function rowOfChosen() {
  const want = props.chosen && props.chosen[props.keyed]
  if (want === undefined || want === null) return -1
  return (grid.value?.viewData || props.rows).findIndex((row) => row[props.keyed] === want)
}

/** The row the keyboard was last reported on. @see announce */
let told = -1

/**
 * Whichever row is under the active cell, reported.
 *
 * `again` is the difference between the mouse and the keyboard. A click is
 * a deliberate act every time -- clicking the same phrase twice means play
 * it twice -- so a click always reports. A key press reports only when the
 * row actually changed, because Left, Right and Tab all fire a keydown on
 * the same row and none of them is a new choice.
 */
function announce(again) {
  const one = grid.value
  const at = one && one.activeCell && one.activeCell.rowIndex
  if (!Number.isInteger(at)) return
  if (!again && at === told) return
  const row = one.viewData[at]
  if (!row) return
  told = at
  emit('pick', row)
}

function build() {
  if (!box.value) return
  grid.value = canvasDatagrid({
    parentNode: box.value,
    schema: props.columns.map((one) => ({ ...one })),
    data: props.rows,
    // A catalogue is read, not written. Editing would let a stray keystroke
    // change a pattern's name in the database.
    editable: false,
    allowColumnReordering: true,
    allowRowReordering: false,
    allowSorting: true,
    showRowNumbers: true,
    showNewRow: false,
    selectionMode: 'row',
    singleSelectionMode: true,
    borderDragBehavior: 'resize',
    columnHeaderClickBehavior: 'sort',
  })

  dress()
  restoreView()
  if (typeof ResizeObserver !== 'undefined') { sizeObserver = new ResizeObserver(queueFit); sizeObserver.observe(box.value) }
  else window.addEventListener('resize', queueFit)
  void nextTick(queueFit)
  for (const event of ['mousedown', 'wheel', 'keydown']) grid.value.addEventListener(event, () => {
    if (restoring) return
    userView = true
    if (event === 'mousedown') draggingView = true
    emit('interaction')
    announceView()
  })
  for (const event of ['scroll', 'sortcolumn', 'resizecolumn', 'reorder']) grid.value.addEventListener(event, announceView)
  grid.value.addEventListener('mouseup', () => { draggingView = false; announceView() })
  // Item drags belong to the instrument, not the spreadsheet selection tool.
  // Cancel the grid event (not NativeEvent) before it sets `selecting = true`.
  let press = null
  const moved = (event) => {
    const native = event.NativeEvent || event.nativeEvent || event
    if (press && Math.hypot(native.clientX - press.x, native.clientY - press.y) > 6) press.moved = true
  }
  grid.value.addEventListener('mousemove', moved)
  grid.value.addEventListener('mousedown', (e) => {
    press = null
    const cell = e.cell, native = e.NativeEvent || e.nativeEvent || e
    if (native.button !== 0 || !cell?.isNormal || !cell.data) return
    if (cell.dragContext && cell.dragContext !== 'cell') return
    e.preventDefault()
    press = { x: native.clientX, y: native.clientY, moved: false }
    const one = grid.value
    one.focus()
    one.clearSelections()
    one.setActiveCell(cell.columnIndex, cell.rowIndex)
    one.selectRow(cell.rowIndex, false, false)
    one.draw()
    if (!['preview', 'load'].includes(cell.header?.name)) emit('drag', cell.data, native)
  })

  /*
   * One click chooses, two uses it -- the same pair as everywhere else in
   * jamin, and the same pair the list it replaces had.
   */
  grid.value.addEventListener('selectionchanged', () => announce(true))

  /*
   * The keyboard moves the active cell and says nothing about it.
   *
   * `selectionchanged` fires when the mouse selects; the arrow keys move
   * the active cell without selecting anything, so walking the list with
   * the keyboard left the book still pointing at whatever was last
   * clicked. Measured in a real browser: four presses of Down took the
   * active cell from row 0 to row 4 and `songPhrase` stayed null, under a
   * line of help text saying "arrow to hear your way through".
   *
   * So the row under the keyboard is read back after the grid has dealt
   * with the key, and reported if it moved. `setTimeout` rather than
   * `nextTick` because it is the grid's own handler being waited for, not
   * Vue's render.
   */
  grid.value.addEventListener('keydown', () => setTimeout(() => announce(false), 0))

  grid.value.addEventListener('dblclick', (event) => {
    const row = event.cell && event.cell.data
    if (row) emit('use', row)
  })

  /*
   * Which column was clicked, as well as which row.
   *
   * A canvas cannot hold a button, so a control that used to be one -- the
   * heart on a phrase -- becomes a column with a mark in it and a click on
   * that column. The book decides what its columns mean; this only says
   * which one was hit.
   */
  grid.value.addEventListener('click', (event) => {
    moved(event)
    const dragged = press?.moved
    press = null
    if (dragged) { event.preventDefault(); return }
    const cell = event.cell
    if (!cell || !cell.data || !cell.header) return
    emit('cell', cell.header.name, cell.data)
  })

  grid.value.addEventListener('contextmenu', (event) => {
    const cell = event.cell
    if (!cell || !cell.data) return
    // The grid's own menu is for a spreadsheet -- ordering columns, hiding
    // them. The books use the right button for something of their own.
    event.preventDefault()
    emit('context', cell.data)
  })

  /*
   * The keyboard, which the grid already has: arrows move the active cell,
   * page up and down move a screen, home and end go to the ends. What it
   * does not have is Enter meaning "use this one", which is what it means
   * in every other list in jamin.
   */
  grid.value.addEventListener('keydown', (event) => {
    const at = grid.value.activeCell && grid.value.activeCell.rowIndex
    const row = Number.isInteger(at) ? grid.value.viewData[at] : null

    if ((event.key || event.NativeEvent?.key) === 'Enter') {
      if (row) emit('use', row)
      return
    }

    /*
     * Everything else the book binds, passed up with the row it is about.
     *
     * The list this replaced had its own keydown handler: the digits put a
     * pattern on a numbered part, space puts it on all of them. Those are
     * the book's shortcuts rather than the grid's, and losing them because
     * the list they were attached to went away would be the change taking
     * something without saying so.
     */
    emit('key', event, row)
  })
}

let restoring = false, userView = false, draggingView = false, viewTimer = 0
function announceView() {
  if (restoring || !userView || draggingView) return
  clearTimeout(viewTimer)
  viewTimer = setTimeout(() => {
    const one = grid.value
    if (!one) return
    userView = false
    const widths = {}
    for (let i = 0; i < props.columns.length; i++) widths[props.columns[i].name] = one.sizes.columns[i] || props.columns[i].width
    emit('view', { top: one.scrollTop, left: one.scrollLeft, orderBy: one.orderBy, direction: one.orderDirection, order: one.columnOrder.map(i => props.columns[i]?.name), widths })
  }, 120)
}
function restoreView() {
  const one = grid.value, value = props.viewState
  if (!one || !value) return
  restoring = true
  try {
    if (value.orderBy && props.columns.some(c => c.name === value.orderBy)) one.order(value.orderBy, value.direction === 'desc' ? 'desc' : 'asc')
    for (let i = 0; i < props.columns.length; i++) {
      const width = value.widths?.[props.columns[i].name]
      if (Number.isFinite(width)) one.sizes.columns[i] = Math.max(30, Math.min(2000, width))
    }
    const order = value.order?.map(name => props.columns.findIndex(c => c.name === name))
    if (order?.length === props.columns.length && new Set(order).size === order.length && order.every(i => i >= 0)) one.columnOrder = order
    one.resize(true)
    one.scrollTop = Math.max(0, Number(value.top) || 0)
    one.scrollLeft = Math.max(0, Number(value.left) || 0)
  } finally { restoring = false }
}
watch(() => props.viewState, restoreView)
onMounted(build)

/*
 * The array grows while it is being read, and is handed over a few times a
 * second rather than on every batch.
 *
 * `data =` is what makes the grid remeasure, and remeasuring is O(the whole
 * list). Doing it once per batch of two thousand is that cost seventy-six
 * times over a list that is getting longer each time, which is most of the
 * twenty-seven seconds a hundred and fifty thousand rows used to take.
 *
 * Nobody can read a list that is growing at forty thousand rows a second
 * anyway. Four times a second is a list that is visibly filling and a
 * thread that is free in between; the trailing call is what makes the last
 * batch arrive rather than being swallowed by the wait.
 *
 * The length as well as the identity: the book grows one array in place
 * now, so the reference no longer changes. @see components/DrumBook.vue
 */
const SETTLE = 250
let handing = 0
let pending = false

function handOver() {
  pending = false
  if (grid.value) { grid.value.data = props.rows; restoreView(); queueFit() }
}

watch(() => [props.rows, props.rows.length], () => {
  if (pending) return
  pending = true
  clearTimeout(handing)
  handing = setTimeout(handOver, SETTLE)
})

watch(() => props.columns, (columns) => {
  if (grid.value) { grid.value.schema = columns.map((one) => ({ ...one })); queueFit() }
})

/*
 * Chosen from somewhere else -- the map, the dice, a keyboard shortcut. The
 * grid has to agree, and has to bring it into view.
 *
 * Unless it is already there, which is the common case and was a bug: an
 * arrow key moves the active cell, the grid says so, the book records the
 * choice, and this put the active cell back where it had just come from.
 * Two presses of Down landed on row one.
 */
watch(() => props.chosen, () => {
  const one = grid.value
  if (!one) return
  const at = rowOfChosen()
  if (at < 0) return
  const here = one.activeCell && one.activeCell.rowIndex
  if (here === at) { told = at; return }
  told = at
  one.setActiveCell(0, at)
  one.scrollIntoView(0, at)
})

watch(currentTheme, dress)

onBeforeUnmount(() => {
  disposed = true; sizeObserver?.disconnect(); cancelAnimationFrame(sizeFrame)
  window.removeEventListener('resize', queueFit)
  clearTimeout(handing)
  clearTimeout(viewTimer)
  const one = grid.value
  grid.value = null
  if (one) one.dispose()
})
</script>

<template>
  <div ref="box" class="jamin-grid"></div>
</template>
<style scoped>
.jamin-grid{position:absolute;inset:0;min-width:0;min-height:0;overflow:hidden}
</style>
