/**
 * A catalogue's tree, explored by opening it.
 *
 * Two measurements decide everything in this file.
 *
 * The first: a real catalogue is 932,299 nodes and the screen holds about
 * fifty. The previous version handed the whole tree to the layout and built a
 * d3 hierarchy over all of it -- three full passes with an object allocated
 * per node -- in order to draw nineteen dots. That was 3.9 seconds to open a
 * window inside a DAW, which is several seconds longer than anybody waits for
 * a plugin before deciding it is broken. So nothing is materialised until it
 * is shown: the arrays are held exactly as the database gave them, and a node
 * becomes an object at the moment somebody opens its parent.
 * @see scripts/graph_perf.py, where those numbers come from.
 *
 * The second: opening a node must not rearrange the picture. A tidy tree
 * cannot do that -- keeping the tree tidy is exactly what makes every sibling
 * move when one node gains children, which is right for a tidy tree and wrong
 * for exploring one. So the layout is a force simulation. Children are born
 * at the point their parent occupies and bloom out of it, whatever is in the
 * way is nudged rather than re-placed, and the camera is not touched at all:
 * where you were looking is where you stay.
 *
 * Canvas rather than SVG, because a wide level of a real catalogue is a few
 * thousand nodes and that many DOM elements is a scroll of jank.
 */
import { select } from 'd3-selection'
import { zoom, zoomIdentity } from 'd3-zoom'
import { quadtree } from 'd3-quadtree'
import { traceShape } from './nodeShapes.js'
import {
  forceSimulation, forceLink, forceManyBody, forceCollide, forceX, forceY,
} from 'd3-force'

/** How near the pointer must be to a node to count as on it, in screen pixels. */
const GRAB = 14

/** How far a ring of children sits from its parent, before the forces argue. */
const REACH = 90

/** The heat a bloom starts with. Enough to move, not enough to throw. */
const BLOOM = 0.7


/** `#rgb` or `#rrggbb` to three numbers in 0..255, or the fallback. */
function readHex(css, fallback) {
  const hex = String(css || '').trim().replace('#', '')
  const full = hex.length === 3 ? hex.split('').map((one) => one + one).join('') : hex
  if (!/^[0-9a-fA-F]{6}$/.test(full)) return fallback
  const n = Number.parseInt(full, 16)
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255]
}

/* Hue, saturation and lightness, because a palette walked in RGB goes
   through grey and a palette walked in HSL stays a palette. */
function rgbToHsl([r, g, b]) {
  const R = r / 255; const G = g / 255; const B = b / 255
  const big = Math.max(R, G, B); const small = Math.min(R, G, B)
  const l = (big + small) / 2
  if (big === small) return [0, 0, l]
  const d = big - small
  const s = l > 0.5 ? d / (2 - big - small) : d / (big + small)
  const h = big === R ? ((G - B) / d + (G < B ? 6 : 0))
    : big === G ? (B - R) / d + 2
      : (R - G) / d + 4
  return [h / 6, s, l]
}

/** A hex colour with an alpha, for the trail wash. */
function withAlpha(css, alpha) {
  const [r, g, b] = readHex(css, [0, 0, 0])
  return `rgba(${r},${g},${b},${Math.max(0, Math.min(1, alpha))})`
}

function hslToRgb([h, s, l]) {
  if (s <= 0) { const v = Math.round(l * 255); return [v, v, v] }
  const q = l < 0.5 ? l * (1 + s) : l + s - l * s
  const p = 2 * l - q
  const one = (t) => {
    let x = t
    if (x < 0) x += 1
    if (x > 1) x -= 1
    if (x < 1 / 6) return p + (q - p) * 6 * x
    if (x < 1 / 2) return q
    if (x < 2 / 3) return p + (q - p) * (2 / 3 - x) * 6
    return p
  }
  return [one(h + 1 / 3), one(h), one(h - 1 / 3)].map((v) => Math.round(v * 255))
}

export class TreeGraph {
  constructor(box, {
    onPick = null, onHover = null, onOpen = null, onArrange = null,
  } = {}) {
    this.box = box
    this.onPick = onPick
    this.onHover = onHover
    this.onOpen = onOpen
    /** Something about where things are changed: a node dropped somewhere,
        the camera moved, the forces come to rest. @see arrangement */
    this.onArrange = onArrange

    this.canvas = document.createElement('canvas')
    /*
     * Underneath the names, explicitly.
     *
     * The labels are a child of this box in the template and the canvas is
     * appended to it on mount, so the canvas is the later sibling -- and two
     * absolutely positioned siblings with no z-index are painted in document
     * order. It got away with that for as long as `paint` began with
     * `clearRect`: a transparent canvas over the names is no canvas at all.
     * Filling the ground for the trails made it opaque, and the names
     * disappeared one frame after they were drawn.
     */
    this.canvas.style.cssText =
      'position:absolute;inset:0;width:100%;height:100%;display:block;z-index:0'
    box.appendChild(this.canvas)
    this.ctx = this.canvas.getContext('2d')

    /** `{ nodes, parents, childAt, childList }`, held, never copied. */
    this.source = null
    /** Which nodes are open, by index. The whole of what is remembered. */
    this.open = new Set()
    /** Index -> the object standing for it on screen. Only the visible ones. */
    this.live = new Map()
    /**
     * What the filters match, by node index, or null for everything.
     *
     * Filtering used to rebuild the tree out of the rows that matched,
     * which is a different tree: different nodes, different indexes, a
     * different layout every time somebody touched a dropdown, and the
     * arrangement they had made thrown away with it. A filter is not a
     * different catalogue. It is the same catalogue with your attention on
     * part of it, so the map stays exactly as it is and the part that
     * matched is the part that is lit. @see setMarked
     */
    this.marked = null
    /** The same as an array, which is what painting and hit-testing walk. */
    this.drawn = []
    this.links = []

    /*
     * How it is drawn, and how it behaves. Set by the view from the theme and
     * the settings; everything here is a default so the renderer works alone.
     */
    this.look = {
      link: 'rgba(128,140,180,0.26)',
      ring: 'rgba(255,255,255,0.9)',
      from: '#7c5cff',
      to: '#22d3ee',
      dim: '#5b6480',
      /** The ground. Black unless somebody says otherwise: a map is a night
          sky, and a lit background throws away half the contrast a glow has
          to work with. */
      ground: '#000000',
      /** How many families the palette is cut into. @see paletteFor */
      families: 7,
      edgeWidth: 0.9,
      nodeSize: 1,
      /** How much of the last frame survives into this one. */
      trail: 0.35,
      /** How far a node's glow reaches past it, as a multiple of its radius. */
      bloom: 0.6,
      /** ring | burst | spiral -- how children leave their parent. */
      unfold: 'ring',
      /** How quickly the layout settles. 1 is the d3 default. */
      speed: 1,
      /** Whether opening something moves the camera to it. */
      follow: false,
      /** How hard nodes push apart, and how far an edge wants to be. */
      repel: 1,
      reach: 1,
      /** How far a node the filters exclude is taken down. 0 leaves the map
          unchanged by a filter; 1 is as near to the background as it goes
          without disappearing. @see colourOf */
      muted: 0.82,
      /** What a clip is drawn as, by what it is. Branches are always
          circles: a folder is not a groove or a fill, and giving it one of
          their shapes would say it was. @see canvas/nodeShapes.js */
      grooveShape: 'circle',
      fillShape: 'square',
    }
    this.palette = []
    this.hovered = -1
    this.chosen = -1
    this.at = zoomIdentity
    this.frame = 0
    /** The auto-arrange animation, if one is running. @see glideTo */
    this.gliding = 0
    this.dpr = 1
    this.width = 1
    this.height = 1
    this.biggest = 1
    this.deepest = 1

    /*
     * The transform is the only thing panning changes.
     *
     * Positions are never touched by looking at the graph, which is what makes
     * it impossible for reading it to move it.
     */
    this.zoom = zoom().scaleExtent([0.02, 40])
      // A drag that starts on a node moves the node; one that starts on the
      // ground moves the view. Without this the two fight and a node cannot
      // be picked up at all.
      .filter((event) => {
        if (event.type === 'wheel') return true
        /*
         * Either button moves the view.
         *
         * d3-zoom takes the left one only, and on a map the size of a
         * catalogue panning is most of what the mouse is for -- having to
         * find a patch of empty space to grab is a thing you notice every
         * few seconds. The right button always pans, wherever it is
         * pressed, so there is always a way to move without hunting for
         * one; the left button still opens a node, so it only pans off a
         * node. @see onDown, which claims the left button on a node.
         */
        if (event.button === 2) return true
        if (event.button) return false
        return !this.spotOf(event)
      })
      .on('zoom', (event) => { this.at = event.transform; this.paintSoon() })
      // Where the camera ended up is part of the arrangement. Only at the
      // end of the gesture -- a wheel is fifty transforms and none of the
      // first forty-nine is where anybody meant to be.
      .on('end', () => { if (this.onArrange) this.onArrange() })
    select(this.canvas).call(this.zoom)

    /** What is being dragged, and everything that came with it. */
    this.dragging = null
    this.canvas.addEventListener('mousedown', this.onDown)
    this.canvas.addEventListener('contextmenu', this.onMenu)
    window.addEventListener('mousemove', this.onDrag)
    window.addEventListener('mouseup', this.onUp)

    /*
     * The forces, and what each one is for.
     *
     * link     holds a child at arm's length from its parent, closer the
     *          deeper it is, so a branch reads as a branch.
     * charge   is the bloom: children born on top of one another push apart
     *          into the ring a force graph is recognised by.
     * collide  is "push other nodes a bit if they are in the way". It acts on
     *          the drawn radius, so a big node takes the room it occupies.
     * x, y     hold the whole picture around the origin, very weakly.
     *
     * These used to pull each node towards its parent, which sounds better
     * and is wrong: d3 reads a force's accessor once, when the nodes are
     * handed over, so "the parent's position" was frozen at the moment of
     * the last rebuild. A node whose parent then moved was pulled towards
     * where its parent used to be, and the picture crept -- measured, the
     * map lost three of its thirty labels over nine seconds as nodes
     * wandered off the edge. Holding branches together is the link force's
     * job and it does it live; all these have to do is stop the whole thing
     * drifting away from the middle.
     */
    this.sim = forceSimulation([])
      .force('link', forceLink([]).id((one) => one.at)
        .distance((one) => REACH / Math.max(1, one.target.depth || 1))
        .strength(0.7))
      .force('charge', forceManyBody().strength(-220).distanceMax(700))
      .force('collide', forceCollide().radius((one) => one.r + 5).iterations(2))
      .force('x', forceX(0).strength(0.012))
      .force('y', forceY(0).strength(0.012))
      .stop()

    this.sim.on('tick', () => { this.tree = null; this.paint() })
    /*
     * A few more frames after it stops, so a trail fades out instead of
     * freezing mid-smear. Without them the last ghost of the last movement
     * stays on screen until something else happens to repaint.
     */
    this.sim.on('end', () => {
      this.cooling = 24
      this.paintSoon()
      /*
       * And this is where the picture is worth writing down.
       *
       * Saving at the moment a folder is clicked saves a layout in mid-air:
       * the forces have another second or two of settling to do, so the
       * arrangement that comes back next session is the one from halfway
       * through the animation rather than the one that was looked at.
       * Measured -- a node saved at 443,847 and read back at 525,956.
       */
      if (this.onArrange) this.onArrange()
    })
    this.cooling = 0

    this.canvas.addEventListener('mousemove', this.onMove)
    this.canvas.addEventListener('mouseleave', this.onLeave)
    this.canvas.addEventListener('click', this.onTap)
    this.canvas.addEventListener('dblclick', this.onDoubleTap)

    this.watching = new ResizeObserver(() => this.resize())
    this.watching.observe(box)
    this.resize()
  }

  /* ---------------- the tree ---------------- */

  /**
   * Take a catalogue's tree, and show its top level.
   *
   * The arrays arrive as the database holds them: `parents`, plus the
   * `childAt`/`childList` pair, which is a compressed child index and the
   * reason a node's children can be found without walking anything. Nothing
   * is copied, and nothing below what is open is looked at.
   *
   * `open` is a list of node indexes to restore, `places` a list of
   * `[at, x, y, pinned]` saying where each of them was left, and `camera` the
   * transform that was looking at them. Together they are the whole of the
   * arrangement: a map comes back exactly as it was left, down to a folder
   * somebody dragged out of the way three days ago.
   * @see components/CatalogueGraph.vue
   */
  setTree(source, { open = null, places = null, camera = null } = {}) {
    this.source = null
    this.live.clear()
    this.open.clear()
    this.chosen = -1
    this.hovered = -1
    // Where each node was left, consulted by `bornAt` as it materialises
    // them. Cleared here so a tree arriving without one starts fresh.
    this.placed = null
    if (places && places.length) {
      this.placed = new Map()
      for (const one of places) {
        if (!Array.isArray(one) || !Number.isFinite(one[1]) || !Number.isFinite(one[2])) continue
        this.placed.set(one[0], one)
      }
    }
    if (this.pinned) this.pinned.clear()

    if (!source || !source.nodes || !source.nodes.length) {
      this.drawn = []
      this.links = []
      this.sim.nodes([])
      this.sim.force('link').links([])
      this.paintSoon()
      return
    }

    this.source = source

    /*
     * One numeric pass for the two things a node's look depends on.
     *
     * Over a million numbers this is a few milliseconds. It was never the
     * scanning that was expensive -- it was allocating an object per node,
     * which is exactly what is no longer done.
     */
    let biggest = 1
    let deepest = 1
    for (const one of source.nodes) {
      if (one.clips > biggest) biggest = one.clips
      if (one.depth > deepest) deepest = one.depth
    }
    this.biggest = Math.log1p(biggest)
    this.deepest = Math.max(1, deepest)

    /*
     * What is open to begin with: whatever was left open, or the top level.
     *
     * "The top level" means the roots' *contents*, not the roots themselves.
     * A catalogue with one library in it has exactly one root, and showing
     * only that is a single dot in the middle of an empty window -- which is
     * technically the top level and is useless. Opening the roots gives the
     * ring of genres or libraries somebody came to look at.
     */
    let asked = 0
    if (open) {
      for (const at of open) {
        if (Number.isInteger(at) && at >= 0 && at < source.nodes.length) {
          this.open.add(at)
          asked++
        }
      }
    }
    if (!asked) {
      /*
       * One root is a library, and a library on its own is a dot in an empty
       * window -- so it is opened, and what you see is the ring of genres
       * inside it. Forty-nine roots are already a top level: opening them all
       * put 700 nodes on screen, which is the thing that was complained
       * about, and cost four seconds to settle.
       */
      const tops = this.roots()
      if (tops.length === 1 && this.hasChildren(tops[0])) this.open.add(tops[0])
    }

    /*
     * Settle, unless there is nothing to settle.
     *
     * The first sight of a catalogue is run to rest before the first paint,
     * so the window opens with a picture rather than with one forming. But
     * a remembered arrangement *is* the rest: running the simulation over it
     * would take every node somebody placed by hand and put it back where
     * the forces would rather have it, which is the arrangement being
     * thrown away with extra steps. So it only settles what it had to
     * invent. @see rebuild
     */
    this.rebuild({ settle: true })
    if (camera && Number.isFinite(camera.k) && camera.k > 0) {
      select(this.canvas).call(this.zoom.transform,
        zoomIdentity.translate(camera.x || 0, camera.y || 0).scale(camera.k))
    } else {
      this.fitView()
    }
  }

  /**
   * Whether everything on screen came back from a remembered place.
   *
   * A node that did not is one the arrangement has nothing to say about --
   * a catalogue that grew, or a folder opened since it was saved -- and the
   * forces have to find it a spot, which means running them, which moves
   * everything else too.
   *
   * Asked after the seats exist, not before. The first version asked in
   * `setTree`, where `live` has just been cleared, so it always answered no
   * and the arrangement was settled away on every single open.
   */
  restored() {
    if (!this.placed || !this.placed.size || !this.drawn.length) return false
    for (const seat of this.drawn) if (!this.placed.has(seat.at)) return false
    return true
  }

  /**
   * The arrangement, as something that can be written down.
   *
   * Positions are rounded: a map is remembered to the pixel, and eight
   * decimal places of a force simulation's idea of where a dot is are eight
   * characters of somebody's browser storage per node per axis.
   */
  arrangement() {
    const places = []
    for (const seat of this.live.values()) {
      places.push([seat.at, Math.round(seat.x), Math.round(seat.y),
        this.pinned && this.pinned.has(seat.at) ? 1 : 0])
    }
    return {
      open: [...this.open],
      places,
      camera: { x: Math.round(this.at.x), y: Math.round(this.at.y),
        k: Math.round(this.at.k * 10000) / 10000 },
    }
  }

  /** Every node with no parent: the level a catalogue opens at. */
  roots() {
    const out = []
    const parents = this.source.parents
    for (let at = 0; at < parents.length; at++) if (parents[at] < 0) out.push(at)
    return out
  }

  /** One node's children, straight out of the compressed index. */
  childrenOf(at) {
    const source = this.source
    if (!source || !source.childAt || at < 0 || at + 1 >= source.childAt.length) return []
    const out = []
    for (let i = source.childAt[at]; i < source.childAt[at + 1]; i++) {
      out.push(source.childList[i])
    }
    return out
  }

  hasChildren(at) {
    const source = this.source
    if (!source || !source.childAt || at < 0 || at + 1 >= source.childAt.length) return false
    return source.childAt[at + 1] > source.childAt[at]
  }

  /**
   * Which nodes should be on screen, and what each one looks like.
   *
   * Breadth-first from the roots, following only what is open, so the cost is
   * the size of what is shown rather than the size of the catalogue. A node
   * already on screen keeps the position it has -- that is what makes opening
   * one leave the others where they were -- and a new one is born at its
   * parent's point, so the bloom starts from the thing it came out of.
   */
  rebuild({ settle = false, heat = BLOOM } = {}) {
    if (!this.source) return

    const wanted = this.roots()
    for (let i = 0; i < wanted.length; i++) {
      const at = wanted[i]
      if (!this.open.has(at)) continue
      for (const child of this.childrenOf(at)) wanted.push(child)
    }

    const keep = new Set(wanted)
    for (const at of [...this.live.keys()]) if (!keep.has(at)) this.live.delete(at)

    const spread = Math.max(1, wanted.length)
    for (const at of wanted) if (!this.live.has(at)) this.live.set(at, this.bornAt(at, spread))

    for (const seat of this.live.values()) {
      seat.shut = this.hasChildren(seat.at) && !this.open.has(seat.at)
    }

    this.drawn = [...this.live.values()]
    this.links = []
    for (const seat of this.drawn) {
      const up = this.source.parents[seat.at]
      if (up >= 0 && this.live.has(up)) this.links.push({ source: up, target: seat.at })
    }

    this.tree = null
    this.sim.nodes(this.drawn)
    this.sim.force('link').links(this.links)

    /*
     * Nothing to work out, so nothing is worked out.
     *
     * Every node on screen is on a remembered position. Running the
     * simulation over that -- to settle it, or to bloom it -- would take
     * each one a little way towards where the forces would rather have it,
     * and a map somebody arranged by hand would dissolve over a second or
     * two of watching. So the forces are simply not started.
     */
    if (this.restored()) {
      this.sim.alpha(0)
      this.sim.stop()
      this.paintSoon()
      return
    }

    if (settle) {
      /*
       * The first sight of a catalogue is not an animation of it arriving.
       *
       * Run to rest before the first paint, so the window opens with a
       * picture in it rather than with a picture forming. Only here: every
       * later change is a bloom, which is the part worth watching.
       */
      this.sim.alpha(1)
      for (let i = 0; i < 180; i++) this.sim.tick()
      this.sim.stop()
      this.paintSoon()
      return
    }

    this.sim.alpha(heat).restart()
  }

  /**
   * A node's first appearance: on top of its parent, barely nudged.
   *
   * Barely, but not exactly. A dozen children born at the identical point
   * have no direction to separate in, and a perfectly symmetric pile is
   * something a force simulation cannot break on its own. The nudge comes
   * from the node's own index, so it is the same every time -- a map rebuilt
   * from the same tree comes out the same way round, which is what lets
   * somebody recognise it.
   */
  bornAt(at, spread) {
    const node = this.source.nodes[at]
    const up = this.live.get(this.source.parents[at])
    /*
     * Where it comes from, as an angle.
     *
     * A child is nudged off its parent by its own index, which spreads a
     * folder's contents around it and is the whole reason a bloom opens
     * out rather than staying a pile.
     *
     * A root has no parent to be nudged off, and the same arithmetic gives
     * neighbouring roots neighbouring angles: nodes 0 to 7 all leave along
     * the same ray within a fortieth of a radian, and collide then pushes
     * them apart *along* it. Eight top-level nodes came out as a vertical
     * line. A catalogue with one root never showed it; the progression map
     * has one per shape. So the roots share the circle between them.
     */
    const roots = this.source.parents[at] < 0 ? this.roots() : null
    const among = roots ? roots.indexOf(at) : -1
    const angle = among >= 0 && roots.length > 1
      ? (among / roots.length) * Math.PI * 2
      : ((at % 997) / 997) * Math.PI * 2

    /*
     * Unless it has been here before.
     *
     * A node with a remembered place is put straight back on it, and one
     * that was pinned there by hand is pinned again -- otherwise the first
     * tick of the simulation would pull it towards its parent and the
     * arrangement would dissolve over about a second.
     */
    const was = this.placed && this.placed.get(at)
    if (was) {
      const seat = {
        at,
        depth: node.depth,
        clips: node.clips,
        label: node.label,
        leaf: node.leaf,
        family: this.familyOf(at),
        r: this.radiusOf(node),
        shape: this.shapeOf(node),
        dim: this.marked ? !this.marked.has(at) : false,
        colour: this.colourOf(node, this.familyOf(at),
          this.marked ? !this.marked.has(at) : false),
        x: was[1],
        y: was[2],
        shut: this.hasChildren(at) && !this.open.has(at),
      }
      if (was[3]) {
        seat.fx = was[1]
        seat.fy = was[2]
        this.pinned = this.pinned || new Set()
        this.pinned.add(at)
      }
      return seat
    }

    /*
     * How a child leaves its parent.
     *
     * `ring` sets them all a hair away and lets the forces open them out,
     * which is the steadiest. `burst` throws them clear so the opening reads
     * as an event. `spiral` fans them by index, which keeps a big folder
     * legible while it settles instead of untangling for a second first.
     */
    const how = this.look.unfold || 'ring'
    const room = how === 'burst' ? 34 : how === 'spiral' ? 10 + (at % 17) * 2 : 6
    const push = up ? room : (REACH * Math.sqrt(spread)) / 2

    return {
      at,
      depth: node.depth,
      clips: node.clips,
      label: node.label,
      leaf: node.leaf,
      family: this.familyOf(at),
      r: this.radiusOf(node),
      shape: this.shapeOf(node),
      dim: this.marked ? !this.marked.has(at) : false,
      colour: this.colourOf(node, this.familyOf(at),
        this.marked ? !this.marked.has(at) : false),
      x: (up ? up.x : 0) + Math.cos(angle) * push,
      y: (up ? up.y : 0) + Math.sin(angle) * push,
      shut: this.hasChildren(at) && !this.open.has(at),
    }
  }

  radiusOf(node) {
    const where = Math.log1p(Math.max(1, node.clips)) / Math.max(1e-6, this.biggest)
    return Math.max(1.5, (2 + where * 8) * (this.look.nodeSize || 1))
  }

  /**
   * What this node is drawn as.
   *
   * Only a clip has a kind, and only the drum catalogue records one, so
   * everything else -- folders, shelves, phrases, progressions -- is a
   * circle. `fill` is put on the leaf when the map is drawn, off the row's
   * own field; nothing here guesses from a name. @see core/pathTree.js
   */
  shapeOf(node) {
    if (!node || !node.leaf) return 'circle'
    return (node.fill ? this.look.fillShape : this.look.grooveShape) || 'circle'
  }

  /**
   * The theme's own colours, cut into a handful of families.
   *
   * Not a rainbow. Every shade here lies on the arc between the theme's two
   * accents, walked in hue-saturation-lightness rather than in RGB -- the
   * straight RGB line between two saturated colours passes through mud, and
   * the mud is what made an earlier ramp look like a different program's
   * palette. A theme whose accents are two greys therefore gets greys, which
   * is the point: the map should look like the rest of jamin looks.
   *
   * A family per branch rather than a shade per depth. Depth is already said
   * by how far out a node is and by how big it is; what the eye cannot get
   * from the picture is which branch a far-flung node belongs to, and colour
   * is the one channel that can say it at a glance.
   */
  paletteFor(many) {
    const from = rgbToHsl(readHex(this.look.from, [124, 92, 255]))
    const to = rgbToHsl(readHex(this.look.to, [34, 211, 238]))

    // The short way round the wheel, so two accents either side of red do not
    // travel through every hue between them.
    let turn = to[0] - from[0]
    if (turn > 0.5) turn -= 1
    if (turn < -0.5) turn += 1

    const out = []
    for (let i = 0; i < many; i++) {
      const along = many === 1 ? 0 : i / (many - 1)
      out.push([
        (from[0] + turn * along + 1) % 1,
        from[1] + (to[1] - from[1]) * along,
        from[2] + (to[2] - from[2]) * along,
      ])
    }
    return out
  }

  /**
   * One node's colour: its family, lit by how deep it is.
   *
   * Deeper is dimmer and less saturated, so the structure holding a
   * catalogue up reads through the clips hanging off it -- the leaves are
   * most of a real tree, and at full strength they are the entire picture.
   */
  colourOf(node, family, dim = false) {
    if (!this.palette.length) this.palette = this.paletteFor(this.look.families || 7)
    const [h, s, l] = this.palette[((family % this.palette.length) + this.palette.length)
      % this.palette.length]
    const down = Math.min(1, node.depth / Math.max(1, this.deepest))
    const fade = node.leaf ? 0.45 : down * 0.3
    /*
     * Outside the filter: still there, and plainly not the point.
     *
     * Dark enough to read as background and light enough to keep the shape
     * of the catalogue -- a node the filter excluded is not a node that has
     * gone away, and a map that hid them would be the layout changing by
     * another name. Colour is drained before lightness so a dimmed branch
     * reads as grey rather than as a darker version of its own family,
     * which at a distance looks like a branch that simply went quiet.
     */
    const mute = dim ? Math.max(0, Math.min(1, this.look.muted ?? 0.82)) : 0
    const [r, g, b] = hslToRgb([
      h,
      s * (1 - fade * 0.55) * (1 - mute * 0.9),
      l * (1 - fade * 0.4) * (1 - mute * 0.72),
    ])
    return `rgb(${r},${g},${b})`
  }

  /**
   * Light these and dim the rest, without moving anything.
   *
   * `null` is "everything", which is what no filter means. Only the colours
   * are touched -- not the positions, not the open set, not the camera --
   * so changing a filter costs one pass over what is on screen.
   */
  setMarked(marked) {
    // `null` is no filter; an empty set is a filter that matched nothing,
    // which dims the whole map. Treating the two the same lit everything
    // up at the moment a filter found nothing, which reads as the filter
    // having been ignored.
    this.marked = marked || null
    for (const seat of this.live.values()) {
      seat.dim = this.marked ? !this.marked.has(seat.at) : false
      seat.colour = this.colourOf(this.source.nodes[seat.at], seat.family, seat.dim)
    }
    this.paintSoon()
  }

  /** Whether the filters have anything to say about this node. */
  litAt(at) {
    return !this.marked || this.marked.has(at)
  }

  /** Which branch a node belongs to: its own index at depth one, or the root's. */
  familyOf(at) {
    let here = at
    let guard = 0
    while (guard++ < 64) {
      const up = this.source.parents[here]
      if (up < 0 || this.source.parents[up] < 0) break
      here = up
    }
    return here
  }

  /* ---------------- opening and closing ---------------- */

  /**
   * Open one node, or close it. Nothing else on screen is disturbed.
   *
   * No camera move and no rebuild of anything already placed: the children
   * appear where their parent is and the simulation is given enough heat to
   * bloom them out. Closing takes them away and lets the gap shut.
   */
  toggle(at) {
    if (!this.source || !Number.isInteger(at) || at < 0) return false
    if (this.open.has(at)) this.open.delete(at)
    else if (this.hasChildren(at)) this.open.add(at)
    else return false

    this.rebuild()
    // Only if asked. Moving the camera to what was just clicked is the one
    // thing an explorer must not do by default -- you clicked it because you
    // could see it, and taking it somewhere else loses the place.
    if (this.look.follow && this.open.has(at)) this.zoomToPoint(at, this.at.k)
    if (this.onOpen) this.onOpen(at, this.open.has(at))
    return true
  }

  /** Everything one more level down, for looking around rather than for. */
  openMore() {
    if (!this.source) return
    for (const seat of [...this.drawn]) if (this.hasChildren(seat.at)) this.open.add(seat.at)
    this.rebuild({ heat: 0.9 })
    if (this.onOpen) this.onOpen(-1, true)
  }

  closeAll() {
    if (!this.source) return
    // And whatever was dragged into place. Collapsing is the "start again"
    // gesture, and a pinned node surviving it would be a ghost of an
    // arrangement nobody can see any more.
    for (const seat of this.live.values()) { seat.fx = null; seat.fy = null }
    if (this.pinned) this.pinned.clear()
    this.open.clear()
    this.chosen = -1
    this.rebuild({ heat: 0.4 })
    if (this.onOpen) this.onOpen(-1, false)
  }

  /** The way back up, as node indexes from the root down. */
  ancestorsOf(at) {
    const out = []
    let here = at
    let guard = 0
    while (this.source && here >= 0 && guard++ < 64) {
      out.unshift(here)
      here = this.source.parents[here]
    }
    return out
  }

  /* ---------------- where things are ---------------- */

  placeOf(seat) { return [seat.x, seat.y] }

  screenOf(seat) { return this.at.apply([seat.x, seat.y]) }

  /**
   * How much of the canvas is actually clear.
   *
   * The canvas runs the whole window and the interface floats on top of it: a
   * bar of filters across the top, a detail panel down the right, a count
   * along the bottom. Fitting to the canvas fits to a box a third of which
   * cannot be seen. Each floating piece says which edge it is on, because
   * working that out from the geometry was tried and was wrong -- a panel
   * down the right-hand side sits against the bottom edge as snugly as the
   * right one, and the graph ended up squeezed into a corner.
   */
  clearArea() {
    const box = { top: 0, right: 0, bottom: 0, left: 0 }
    const host = this.canvas && this.canvas.parentElement
    if (!host || !host.ownerDocument) return box

    const mine = this.canvas.getBoundingClientRect()
    if (!mine.width || !mine.height) return box

    for (const one of host.ownerDocument.querySelectorAll('[data-keep-clear]')) {
      const side = one.getAttribute('data-keep-clear')
      if (!Object.prototype.hasOwnProperty.call(box, side)) continue

      const there = one.getBoundingClientRect()
      if (!there.width || !there.height) continue
      if (there.right <= mine.left || there.left >= mine.right) continue
      if (there.bottom <= mine.top || there.top >= mine.bottom) continue

      /*
       * A panel down one side only counts if it is down one side.
       *
       * The detail panel is as tall as what is in it, and with nothing
       * picked that is about a hundred pixels -- reserving the whole
       * right-hand column for it would give away a third of the map to
       * something occupying a tenth of that edge. So a side panel shorter
       * than a third of the canvas is treated as floating rather than as an
       * edge, and the fit ignores it. Whether that ever hides a node is
       * asserted rather than assumed. @see scripts/shots.py eachCatalogue
       */
      const sideways = side === 'left' || side === 'right'
      if (sideways && there.height < mine.height / 3) continue

      const deep = side === 'top' ? there.bottom - mine.top
        : side === 'bottom' ? mine.bottom - there.top
          : side === 'left' ? there.right - mine.left
            : mine.right - there.left
      const most = (sideways ? mine.width : mine.height) * 0.5
      box[side] = Math.max(box[side], Math.min(deep, most))
    }
    return box
  }

  fitView(padding = 80) {
    if (!this.drawn.length || !this.width) return
    let lowX = Infinity; let lowY = Infinity; let highX = -Infinity; let highY = -Infinity
    for (const one of this.drawn) {
      if (one.x < lowX) lowX = one.x
      if (one.x > highX) highX = one.x
      if (one.y < lowY) lowY = one.y
      if (one.y > highY) highY = one.y
    }
    const wide = Math.max(1, highX - lowX)
    const tall = Math.max(1, highY - lowY)

    const clear = this.clearArea()
    const room = Math.max(120, this.width - clear.left - clear.right - padding * 2)
    const high = Math.max(120, this.height - clear.top - clear.bottom - padding * 2)
    const midX = clear.left + (this.width - clear.left - clear.right) / 2
    const midY = clear.top + (this.height - clear.top - clear.bottom) / 2

    const scale = Math.max(0.02, Math.min(40, Math.min(room / wide, high / tall)))
    select(this.canvas).call(this.zoom.transform, zoomIdentity
      .translate(midX, midY)
      .scale(scale)
      .translate(-(lowX + wide / 2), -(lowY + tall / 2)))
  }

  /** Put one node in the middle, at a readable size. */
  zoomToPoint(at, scale = 1.4) {
    const seat = this.live.get(at)
    if (!seat) return
    select(this.canvas).call(this.zoom.transform, zoomIdentity
      .translate(this.width / 2, this.height / 2)
      .scale(Math.max(0.02, Math.min(40, scale)))
      .translate(-seat.x, -seat.y))
  }

  index() {
    if (this.tree) return this.tree
    this.tree = quadtree().x((one) => one.x).y((one) => one.y).addAll(this.drawn)
    return this.tree
  }

  nodeAt(screenX, screenY) {
    if (!this.drawn.length) return null
    const [x, y] = this.at.invert([screenX, screenY])
    return this.index().find(x, y, GRAB / this.at.k) || null
  }

  spotOf(event) {
    const box = this.canvas.getBoundingClientRect()
    return this.nodeAt(event.clientX - box.left, event.clientY - box.top)
  }

  onMove = (event) => {
    const found = this.spotOf(event)
    const which = found ? found.at : -1
    if (which === this.hovered) return
    this.hovered = which
    this.canvas.style.cursor = which >= 0 ? 'pointer' : 'default'
    if (this.onHover) this.onHover(found || null)
    this.paintSoon()
  }

  /**
   * Picking a node up.
   *
   * The branch comes with it: dragging a folder and watching its contents
   * stay behind is the picture coming apart, not the folder moving. Every
   * descendant on screen is carried by the same offset, and the forces are
   * left running so whatever is in the way gets out of it.
   */
  onDown = (event) => {
    // Left only: the right one is panning, even over a node.
    if (event.button !== 0) return
    const found = this.spotOf(event)
    if (!found) return

    const carried = this.descendantsOf(found.at)
    this.dragging = {
      at: found.at,
      from: this.at.invert([event.clientX - this.canvas.getBoundingClientRect().left,
        event.clientY - this.canvas.getBoundingClientRect().top]),
      moved: 0,
      held: carried.map((seat) => ({ seat, x: seat.x, y: seat.y })),
    }
    for (const { seat } of this.dragging.held) { seat.fx = seat.x; seat.fy = seat.y }
    this.sim.alphaTarget(0.12).restart()
    event.preventDefault()
  }

  onDrag = (event) => {
    if (!this.dragging) return
    const box = this.canvas.getBoundingClientRect()
    const [x, y] = this.at.invert([event.clientX - box.left, event.clientY - box.top])
    const dx = x - this.dragging.from[0]
    const dy = y - this.dragging.from[1]
    this.dragging.moved = Math.max(this.dragging.moved, Math.hypot(dx, dy) * this.at.k)

    for (const one of this.dragging.held) {
      one.seat.fx = one.x + dx
      one.seat.fy = one.y + dy
      one.seat.x = one.seat.fx
      one.seat.y = one.seat.fy
    }
    this.tree = null
    this.paintSoon()
  }

  onUp = () => {
    if (!this.dragging) return
    /*
     * Dropped, and left there.
     *
     * The node keeps the place it was put -- that is what repositioning
     * means, and a graph that springs back the moment you let go cannot be
     * arranged. Its children are let go of, so the branch relaxes around the
     * new position instead of staying in the rigid shape it was carried in.
     */
    const { at } = this.dragging
    for (const one of this.dragging.held) {
      if (one.seat.at === at) continue
      one.seat.fx = null
      one.seat.fy = null
    }
    this.pinned = this.pinned || new Set()
    this.pinned.add(at)
    this.sim.alphaTarget(0).alpha(0.3).restart()
    this.wasDrag = this.dragging.moved > 4
    this.dragging = null
    // Saved after the branch has relaxed around where it was dropped, not
    // at the instant of dropping: the positions worth keeping are the ones
    // that are still there a moment later.
    // The simulation's own `end` covers the usual case; this covers a drop
    // that moved nothing far enough to reheat it.
    if (this.onArrange) setTimeout(() => this.onArrange(), 900)
  }

  /**
   * Run the forces to a stop now, rather than over the next few seconds.
   *
   * The same thing the first sight of a catalogue does, offered as a verb:
   * a settled picture is the one worth keeping, and waiting five seconds of
   * animation to have one is a wait. Bounded, because a simulation that has
   * not converged in four hundred ticks is not going to.
   */
  rest() {
    if (!this.drawn.length) return
    const floor = this.sim.alphaMin()
    for (let i = 0; i < 400 && this.sim.alpha() > floor; i++) this.sim.tick()
    this.sim.alpha(0)
    this.sim.stop()
    this.paintSoon()
    if (this.onArrange) this.onArrange()
  }

  /**
   * Lay what is on screen out as a tree, rather than as a settled cloud.
   *
   * The forces make a picture that is honest about how much is where and
   * is, on a real catalogue, a knot: branches cross, a folder ends up
   * nowhere near its parent, and the shape of the thing is guesswork. A
   * tree is not a cloud. Drawn as a tree it has the shape it actually
   * has -- the root in the middle, each level a ring further out, every
   * branch its own wedge, and no two branches overlapping.
   *
   * The wedges are shared out by how many leaves are under each child
   * rather than by how many children there are, so a folder holding four
   * hundred clips gets the room it needs and one holding two does not get
   * the same. That is the whole difference between a radial tree that
   * reads and one that is a fan of overlapping spokes.
   *
   * The ring spacing comes from the outermost ring: it has to be long
   * enough for every leaf on it to sit clear of its neighbours, which is a
   * circumference, which is a radius. Nothing is guessed at a fixed size.
   *
   * Animated, because the point of arranging a map somebody is reading is
   * that they can see what moved where. @see glideTo
   */
  arrange() {
    if (!this.drawn.length || !this.source) return

    const kids = new Map()
    const roots = []
    for (const seat of this.drawn) {
      const up = this.source.parents[seat.at]
      if (up >= 0 && this.live.has(up)) {
        const list = kids.get(up)
        if (list) list.push(seat.at)
        else kids.set(up, [seat.at])
      } else {
        roots.push(seat.at)
      }
    }
    // The catalogue's own order, which is what the index is. The live map
    // is in the order things happened to be opened in, and a ring in that
    // order is a ring nobody can read down.
    for (const list of kids.values()) list.sort((a, b) => a - b)
    roots.sort((a, b) => a - b)

    /*
     * How much room each branch needs, counted in leaves.
     *
     * Bottom up over a list that is already parent-before-child, so one
     * pass backwards does it -- the same trick the tree itself is built
     * with. A node with nothing open under it counts as one.
     */
    const order = []
    const stack = [...roots]
    while (stack.length) {
      const at = stack.pop()
      order.push(at)
      const under = kids.get(at)
      if (under) for (const child of under) stack.push(child)
    }
    const weight = new Map()
    for (let i = order.length - 1; i >= 0; i--) {
      const at = order[i]
      const under = kids.get(at)
      if (!under || !under.length) { weight.set(at, 1); continue }
      let sum = 0
      for (const child of under) sum += weight.get(child) || 1
      weight.set(at, sum)
    }

    // Deep enough, and wide enough, to hold what is on it.
    let deepest = 0
    const levelOf = new Map()
    for (const at of roots) levelOf.set(at, 0)
    for (const at of order) {
      const here = levelOf.get(at) || 0
      deepest = Math.max(deepest, here)
      for (const child of kids.get(at) || []) levelOf.set(child, here + 1)
    }

    const leaves = roots.reduce((sum, at) => sum + (weight.get(at) || 1), 0)
    const GAP = 30
    const rings = Math.max(1, deepest)
    const ring = Math.max(REACH, (leaves * GAP) / (2 * Math.PI * rings))

    const target = new Map()
    const place = (at, from, to) => {
      const level = levelOf.get(at) || 0
      const middle = (from + to) / 2
      const radius = level * ring
      target.set(at, level === 0 && roots.length === 1
        ? [0, 0]
        : [Math.cos(middle) * radius, Math.sin(middle) * radius])

      const under = kids.get(at)
      if (!under || !under.length) return
      const span = to - from
      const total = weight.get(at) || 1
      let edge = from
      for (const child of under) {
        const share = (span * (weight.get(child) || 1)) / total
        place(child, edge, edge + share)
        edge += share
      }
    }

    /*
     * A single root sits in the middle and its children take the whole
     * circle. Several roots are a top level in their own right and share
     * it between them -- with a gap, so the first and the last are not on
     * the same angle, which is the seam a radial layout gets wrong first.
     */
    if (roots.length === 1) {
      place(roots[0], 0, Math.PI * 2)
    } else {
      let edge = 0
      for (const at of roots) {
        const share = (Math.PI * 2 * (weight.get(at) || 1)) / Math.max(1, leaves)
        place(at, edge, edge + share)
        edge += share
      }
    }

    this.glideTo(target)
  }

  /**
   * Move everything to where it is going, over about two thirds of a
   * second.
   *
   * Not a jump. A map somebody is reading that rearranges between one
   * frame and the next is a different map they now have to find their way
   * around again; watching it move is what makes it the same one. Eased at
   * both ends, and anything pinned by hand is let go of, because being
   * arranged is the opposite of being held.
   */
  glideTo(target) {
    const from = new Map()
    for (const seat of this.drawn) {
      from.set(seat.at, [seat.x, seat.y])
      seat.fx = null
      seat.fy = null
    }
    if (this.pinned) this.pinned.clear()
    this.sim.stop()
    this.sim.alpha(0)
    cancelAnimationFrame(this.gliding)

    const began = performance.now()
    const OVER = 680
    const step = () => {
      const t = Math.min(1, (performance.now() - began) / OVER)
      // Smoothstep: no lurch at either end.
      const e = t * t * (3 - 2 * t)
      for (const seat of this.drawn) {
        const was = from.get(seat.at)
        const to = target.get(seat.at)
        if (!was || !to) continue
        seat.x = was[0] + (to[0] - was[0]) * e
        seat.y = was[1] + (to[1] - was[1]) * e
      }
      this.tree = null
      this.paint()
      if (t < 1) { this.gliding = requestAnimationFrame(step); return }
      this.gliding = 0
      /*
       * And a few dozen frames to wash the trails out.
       *
       * A trail is the last frame painted over rather than wiped, so when
       * the painting stops the last smear stays -- and after a move this
       * long that is a ghost of the whole old layout hanging behind the
       * new one. The frames cost nothing and the fade is the point.
       */
      this.cooling = 48
      this.fitView()
      this.paintSoon()
      if (this.onArrange) this.onArrange()
    }
    this.gliding = requestAnimationFrame(step)
  }

  /** Every node on screen under this one, itself included. */
  descendantsOf(at) {
    const out = []
    const queue = [at]
    for (let i = 0; i < queue.length; i++) {
      const seat = this.live.get(queue[i])
      if (!seat) continue
      out.push(seat)
      for (const child of this.childrenOf(queue[i])) {
        if (this.live.has(child)) queue.push(child)
      }
    }
    return out
  }

  onLeave = () => {
    if (this.hovered < 0) return
    this.hovered = -1
    if (this.onHover) this.onHover(null)
    this.paintSoon()
  }

  /**
   * One click picks it and opens it.
   *
   * It used to take a double-click, on the reasoning that a single click is
   * for choosing and a double for opening. Nobody double-clicks a graph node:
   * one that does nothing when clicked reads as a picture rather than as a
   * control, which is what "clicking, double clicking, does nothing" was
   * describing. So a click does both, and clicking again closes it.
   */
  onTap = (event) => {
    // A drag ends in a click event too. Four pixels is further than a hand
    // moves while pressing a button and nowhere near a deliberate drag.
    if (this.wasDrag) { this.wasDrag = false; return }
    const found = this.spotOf(event)
    if (!found) return
    this.chosen = found.at
    if (this.onPick) this.onPick(found)
    this.toggle(found.at)
    this.paintSoon()
  }

  onDoubleTap = (event) => {
    // The two clicks of a double have already opened it and closed it again.
    // Swallowed rather than acted on a third time.
    event.preventDefault()
  }

  /*
   * No menu on the map.
   *
   * The right button pans, and a browser that opens its own menu halfway
   * through a drag leaves the drag stuck to the pointer.
   */
  onMenu = (event) => { event.preventDefault() }

  /* ---------------- painting it ---------------- */

  resize() {
    const box = this.box.getBoundingClientRect()
    this.dpr = Math.min(2, window.devicePixelRatio || 1)
    this.width = Math.max(1, Math.round(box.width))
    this.height = Math.max(1, Math.round(box.height))
    this.canvas.width = Math.round(this.width * this.dpr)
    this.canvas.height = Math.round(this.height * this.dpr)
    this.paintSoon()
  }

  paintSoon() {
    if (this.frame) return
    this.frame = requestAnimationFrame(() => { this.frame = 0; this.paint() })
  }

  /**
   * The dials, applied.
   *
   * Called whenever the view hands over new settings. The forces are
   * reconfigured in place rather than rebuilt, so nothing already on screen
   * moves because somebody dragged a slider -- the picture just starts
   * behaving differently.
   */
  retune() {
    const look = this.look
    this.palette = this.paletteFor(look.families || 7)

    const speed = Math.max(0.15, Math.min(4, look.speed || 1))
    this.sim.alphaDecay(0.0228 * speed)
    this.sim.velocityDecay(Math.max(0.05, Math.min(0.9, 0.4 / Math.sqrt(speed))))

    this.sim.force('charge').strength(-220 * Math.max(0.1, look.repel ?? 1))
    this.sim.force('link').distance((one) =>
      (REACH * Math.max(0.2, look.reach ?? 1)) / Math.max(1, one.target.depth || 1))

    for (const seat of this.live.values()) {
      seat.r = this.radiusOf(this.source.nodes[seat.at])
      seat.shape = this.shapeOf(this.source.nodes[seat.at])
      seat.dim = this.marked ? !this.marked.has(seat.at) : false
      seat.colour = this.colourOf(this.source.nodes[seat.at], seat.family, seat.dim)
    }
    this.paintSoon()
  }

  paint() {
    const ctx = this.ctx
    if (!ctx) return
    ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0)

    /*
     * Trails: the last frame is painted over rather than wiped.
     *
     * A translucent wash of the ground leaves a fading ghost of where things
     * were, which is what makes a bloom look like motion instead of like a
     * sequence of stills. At trail 0 the wash is opaque and this is an
     * ordinary clear.
     */
    const trail = Math.max(0, Math.min(0.92, this.look.trail ?? 0))
    if (trail > 0.01) {
      ctx.globalCompositeOperation = 'source-over'
      ctx.fillStyle = withAlpha(this.look.ground, 1 - trail)
      ctx.fillRect(0, 0, this.width, this.height)
    } else {
      ctx.fillStyle = this.look.ground || '#000'
      ctx.fillRect(0, 0, this.width, this.height)
    }
    if (this.cooling > 0) {
      this.cooling--
      this.paintSoon()
    }
    if (!this.drawn.length) return

    const { x, y, k } = this.at
    ctx.save()
    ctx.translate(x, y)
    ctx.scale(k, k)

    // Every edge in one path and one stroke. d3's link force replaces the
    // indexes with the nodes themselves, so this reads either.
    ctx.beginPath()
    for (const link of this.links) {
      const from = typeof link.source === 'object' ? link.source : this.live.get(link.source)
      const to = typeof link.target === 'object' ? link.target : this.live.get(link.target)
      if (!from || !to) continue
      ctx.moveTo(from.x, from.y)
      ctx.lineTo(to.x, to.y)
    }
    ctx.strokeStyle = this.look.link
    ctx.lineWidth = (this.look.edgeWidth ?? 0.9) / k
    ctx.stroke()

    /*
     * Bloom: the same nodes again, wider and faint, added rather than laid
     * over.
     *
     * `lighter` is additive, so where two glows overlap they brighten -- a
     * dense branch lights up as one mass and a lone node is a pinprick,
     * which is the thing a glow is actually for. Drawn first so the crisp
     * node sits inside its own halo rather than under it.
     */
    const bloom = Math.max(0, Math.min(1.5, this.look.bloom ?? 0))
    if (bloom > 0.01) {
      ctx.globalCompositeOperation = 'lighter'
      ctx.globalAlpha = 0.2 * Math.min(1, bloom)
      for (const one of this.drawn) {
        // A halo on a node the filter excluded is the excluded node being
        // the brightest thing on the screen, which is the opposite of what
        // dimming it was for.
        if (one.dim) continue
        ctx.beginPath()
        traceShape(ctx, one.shape, one.x, one.y, one.r * (1 + bloom * 1.5))
        ctx.fillStyle = one.colour
        ctx.fill()
      }
      ctx.globalAlpha = 1
      ctx.globalCompositeOperation = 'source-over'
    }

    for (const one of this.drawn) {
      ctx.beginPath()
      traceShape(ctx, one.shape, one.x, one.y, one.r)
      ctx.fillStyle = one.colour
      ctx.fill()
      // A node with more inside it says so, rather than looking like a leaf.
      // Dimmed along with everything else about it: a white ring is the
      // brightest mark on the map and would pick out exactly the folders
      // the filter had just put aside.
      if (one.shut) {
        ctx.lineWidth = 1.4 / k
        ctx.strokeStyle = one.dim ? 'rgba(255,255,255,0.12)' : 'rgba(255,255,255,0.55)'
        ctx.stroke()
      }
    }

    for (const [at, colour] of [[this.hovered, 'rgba(255,255,255,0.7)'],
      [this.chosen, this.look.ring]]) {
      const seat = this.live.get(at)
      if (!seat) continue
      ctx.beginPath()
      traceShape(ctx, seat.shape, seat.x, seat.y, seat.r + 3.5 / k)
      ctx.strokeStyle = colour
      ctx.lineWidth = 1.8 / k
      ctx.stroke()
    }

    ctx.restore()
  }

  destroy() {
    cancelAnimationFrame(this.frame)
    cancelAnimationFrame(this.gliding)
    this.sim.stop()
    this.watching.disconnect()
    this.canvas.removeEventListener('mousemove', this.onMove)
    this.canvas.removeEventListener('mouseleave', this.onLeave)
    this.canvas.removeEventListener('click', this.onTap)
    this.canvas.removeEventListener('dblclick', this.onDoubleTap)
    this.canvas.removeEventListener('mousedown', this.onDown)
    this.canvas.removeEventListener('contextmenu', this.onMenu)
    window.removeEventListener('mousemove', this.onDrag)
    window.removeEventListener('mouseup', this.onUp)
    select(this.canvas).on('.zoom', null)
    this.canvas.remove()
    this.ctx = null
  }
}
