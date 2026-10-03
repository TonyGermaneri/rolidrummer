/**
 * A catalogue as the tree it already is.
 *
 * The first attempt drew words joined by how often they turn up together, and
 * it was a hairball: every word related to every other, a hundred thousand
 * hairlines, and no way to tell where you were. The mistake was treating a
 * collection of files as a network. It is not a network. It is a tree, and it
 * has been one all along -- libraries, then folders, then folders, then clips.
 *
 * So each node is a *place in the tree*, identified by the whole path down to
 * it. Two folders both called `Rock` in two different libraries are two nodes,
 * because they are two folders; joining them would be asserting a relationship
 * that does not exist and is the sort of thing that makes a hairball.
 *
 * Every node has exactly one parent, so the edge count is the node count. A
 * tree of a million nodes has a million edges where a co-occurrence graph of a
 * thousand had twenty thousand -- and it lays out as something with a shape
 * rather than as a cloud.
 *
 * **Sorting by a facet re-roots it.** With `sortBy: 'genre'` the top level
 * becomes the genres and every library hangs underneath the genre it is in, so
 * the same clips make a different tree. The path is simply prefixed, and
 * everything below works the same way.
 */

/** How the levels are separated internally. Never in a path a vendor wrote. */
const DOWN = '\u0000'

/**
 * Build the tree.
 *
 * `clips` is anything iterable. `pathOf` returns the whole path of one, with
 * `/` between levels; `facetOf` returns the value to re-root by, or nothing.
 *
 * Returns nodes in breadth-first order -- roots first, then their children,
 * then theirs -- which means a renderer can draw the first n and have the top
 * of the tree rather than an arbitrary slice of it.
 */
export function buildTree(clips, {
  pathOf = (one) => one.path,
  facetOf = null,
  /**
   * Whether a clip is a fill rather than a groove, where that means anything.
   *
   * Only the answer `true` is recorded, as `fill` on the leaf. A catalogue of
   * three quarters of a million clips is mostly grooves, and writing
   * `kind: 'beat'` onto every one of them would be most of a megabyte of the
   * stored map saying the default out loud. @see canvas/nodeShapes.js
   */
  fillOf = null,
  mostNodes = 1200000,
  mostChildren = 64,
} = {}) {
  const index = new Map()        // full prefix -> node id
  let nodes = []
  let parents = []
  let truncated = false

  const fresh = (label, parent, depth) => {
    if (nodes.length >= mostNodes) { truncated = true; return -1 }
    const id = nodes.length
    nodes.push({ label, depth, clips: 0, leaf: false })
    parents.push(parent)
    return id
  }

  const nodeFor = (prefix, label, parent, depth) => {
    const held = index.get(prefix)
    if (held !== undefined) return held
    const id = fresh(label, parent, depth)
    if (id >= 0) index.set(prefix, id)
    return id
  }

  for (const clip of clips) {
    const whole = String(pathOf(clip) || '')
    if (!whole) continue

    const facet = facetOf ? String(facetOf(clip) || '') : ''
    // An unlabelled clip is not dropped -- it goes under a level that says so,
    // because a filter offering a genre and a tree hiding everything without
    // one would disagree about the same catalogue.
    const levels = [...(facetOf ? [facet || '(none)'] : []),
                    ...whole.split('/').filter(Boolean)]
    if (!levels.length) continue

    let prefix = ''
    let parent = -1
    for (let depth = 0; depth < levels.length; depth++) {
      prefix = depth ? `${prefix}${DOWN}${levels[depth]}` : levels[depth]

      /*
       * A folder is a shared prefix; a clip is a thing.
       *
       * The last level used to be looked up in the same index as the folders
       * above it, so two clips with the same name in the same place became one
       * node. In a drum library that never happens -- a path names a file. In
       * the phrase catalogue it happens constantly: Impro-Visor has 1,896 licks
       * under 99 names, 345 of them called `minor`, and the map drew one dot
       * for all 345. Eighteen per cent of the catalogue was not on it.
       *
       * It is also cheaper. The index no longer holds an entry per clip, which
       * over three quarters of a million of them is three quarters of a million
       * map entries that never needed to exist.
       */
      const last = depth === levels.length - 1
      const id = last
        ? fresh(levels[depth], parent, depth)
        : nodeFor(prefix, levels[depth], parent, depth)
      if (id < 0) break
      if (last && fillOf && fillOf(clip)) nodes[id].fill = true

      // Every node counts every clip beneath it, which is what makes a folder
      // worth drawing at the size it is.
      nodes[id].clips++
      nodes[id].leaf = depth === levels.length - 1
      parent = id
    }
  }

  // No node with thousands of children, whatever the catalogue is shaped like.
  const capped = capFanOut(nodes, parents, mostChildren)
  if (capped) { nodes = capped.nodes; parents = capped.parents }

  // Parent before child, so a renderer drawing the first n gets the top of the
  // tree rather than a slice from the middle of it.
  const order = breadthFirst(parents)
  const renumber = new Array(nodes.length)
  order.forEach((was, now) => { renumber[was] = now })

  const sorted = order.map((was) => nodes[was])
  const edges = new Uint32Array(Math.max(0, order.length - countRoots(parents)) * 2)

  let at = 0
  for (const was of order) {
    const parent = parents[was]
    if (parent < 0) continue
    edges[at++] = renumber[parent]
    edges[at++] = renumber[was]
  }

  const renumbered = order.map((was) => (parents[was] < 0 ? -1 : renumber[parents[was]]))

  return {
    nodes: sorted,
    edges: edges.subarray(0, at),
    parents: renumbered,
    ...childIndex(renumbered),
    depth: sorted.reduce((most, one) => Math.max(most, one.depth), 0) + 1,
    truncated,
  }
}

/**
 * No node with more children than an eye can take in.
 *
 * Real catalogues are lopsided in ways no taxonomy fixes. One vendor folder
 * holds 1,321 patterns; a chord-progression genre holds tens of thousands with
 * nothing but a decade under it. Drawn, that is a node with a thousand lines
 * radiating from it -- a hairball at one point of an otherwise readable tree,
 * and unreadable exactly where somebody is trying to look.
 *
 * So the wide ones get shelves. Children beyond the limit are grouped into
 * ranges named after what is in them -- `Ab–Ci`, `Cj–Fr` -- in the order the
 * catalogue already has them, and a range that is still too wide is shelved
 * again. The buckets are structure rather than content: nothing is hidden,
 * nothing is invented, and the path to a clip is one or two steps longer.
 *
 * Returns null when nothing needed it, so the common case allocates nothing.
 */
function capFanOut(nodes, parents, most) {
  if (!most || most < 2 || !nodes.length) return null

  const kids = new Map()               // parent (-1 for the roots) -> children
  for (let at = 0; at < parents.length; at++) {
    const parent = parents[at]
    let list = kids.get(parent)
    if (!list) { list = []; kids.set(parent, list) }
    list.push(at)
  }

  let wide = false
  for (const list of kids.values()) if (list.length > most) { wide = true; break }
  if (!wide) return null

  const outNodes = []
  const outParents = []

  // Breadth-first, because everything downstream relies on a parent coming
  // before its children -- the weights are accumulated by walking backwards
  // and the wedges partitioned by walking forwards. @see radialPositions
  const jobs = [{ olds: kids.get(-1) || [], parent: -1, depth: 0 }]
  for (let job = 0; job < jobs.length; job++) {
    const { olds, parent, depth } = jobs[job]

    if (olds.length <= most) {
      for (const was of olds) {
        const id = outNodes.length
        outNodes.push({ ...nodes[was], depth })
        outParents.push(parent)
        const under = kids.get(was)
        if (under && under.length) jobs.push({ olds: under, parent: id, depth: depth + 1 })
      }
      continue
    }

    /*
     * Sorted before they are shelved, so a shelf's name is true.
     *
     * Children keep the catalogue's own order everywhere else, and for a
     * folder that is right. For a shelf it is not: a range named after its
     * first and last child only means anything if what is in between them
     * really is in between. Unsorted, the progression library's genres shelved
     * into `po–me` and `al–re` -- ranges that contain neither end of
     * themselves, which is worse than no label at all.
     */
    const inOrder = olds.slice().sort((a, b) =>
      String(nodes[a].label).localeCompare(String(nodes[b].label)))

    // At most `most` shelves, each holding as few as that allows. A shelf that
    // is still too wide is shelved again by the job it queues for itself.
    const perShelf = Math.ceil(inOrder.length / most)
    for (let from = 0; from < inOrder.length; from += perShelf) {
      const slice = inOrder.slice(from, from + perShelf)
      let clips = 0
      for (const was of slice) clips += nodes[was].clips || 0
      const id = outNodes.length
      outNodes.push({
        label: shelfLabel(nodes[slice[0]].label, nodes[slice[slice.length - 1]].label),
        depth,
        clips,
        leaf: false,
        // So a reader can tell a shelf this made from a folder somebody named.
        shelf: true,
      })
      outParents.push(parent)
      jobs.push({ olds: slice, parent: id, depth: depth + 1 })
    }
  }

  return { nodes: outNodes, parents: outParents }
}

/**
 * What to call a shelf: the range of what is on it.
 *
 * Enough letters to tell one shelf from the next and to be recognisable as the
 * words it stands for -- two was enough for the first and reads as a cipher
 * (`po–me`), and the whole label would be a paragraph on a node the size of a
 * full stop.
 */
function shelfLabel(first, last) {
  const trim = (one) => String(one || '').trim()
  const a = trim(first)
  const b = trim(last)
  if (!a && !b) return '…'
  const short = (one) => (one.length > 6 ? `${one.slice(0, 5)}…` : one)
  return a === b ? short(a) || '…' : `${short(a)} – ${short(b)}`
}

/**
 * A tree read back from storage, with its child index rebuilt.
 *
 * The index is two typed arrays derived from `parents`, and storing them would
 * be storing the same information twice -- so it is not stored, and has to be
 * put back. Without it every node reports no children: nothing offers to open,
 * the keyboard cannot go down, and the view opens on two rings and stops. That
 * was the whole of the bulk drum map, which is the one catalogue big enough to
 * be stored rather than rebuilt.
 */
export function withChildren(tree) {
  if (!tree || !tree.parents) return tree
  if (tree.childAt && tree.childList) return tree
  return { ...tree, ...childIndex(tree.parents) }
}

/**
 * Who each node's children are, laid out flat.
 *
 * Built once, because the alternative is finding them by scanning every node's
 * parent -- which is fine for nine nodes and quadratic for eight hundred
 * thousand. The label pass runs every frame and asks this question twice per
 * frame; at full scale that was two scans of the whole tree, sixty times a
 * second, and a check script doing it once per node never finished at all.
 *
 * Two arrays rather than an array of arrays: `childAt[n]` to `childAt[n + 1]`
 * is the slice of `childList` holding node n's children. Eight hundred thousand
 * small arrays is eight hundred thousand allocations; this is two.
 */
function childIndex(parents) {
  const count = parents.length
  const childAt = new Uint32Array(count + 1)

  // Count each node's children, then turn the counts into start offsets.
  for (const parent of parents) if (parent >= 0) childAt[parent + 1]++
  for (let at = 0; at < count; at++) childAt[at + 1] += childAt[at]

  const childList = new Uint32Array(Math.max(0, childAt[count]))
  const filled = new Uint32Array(count)
  parents.forEach((parent, child) => {
    if (parent < 0) return
    childList[childAt[parent] + filled[parent]] = child
    filled[parent]++
  })

  return { childAt, childList }
}

function countRoots(parents) {
  let n = 0
  for (const one of parents) if (one < 0) n++
  return n
}

/** Roots, then their children, then theirs. */
function breadthFirst(parents) {
  const children = Array.from({ length: parents.length }, () => [])
  const roots = []
  parents.forEach((parent, at) => {
    if (parent < 0) roots.push(at)
    else children[parent].push(at)
  })

  // Read with a moving index rather than `shift()`. Removing the front of an
  // array is not free, and a tree of eight hundred thousand nodes would do it
  // eight hundred thousand times.
  const queue = [...roots]
  for (let at = 0; at < queue.length; at++) {
    for (const child of children[queue[at]]) queue.push(child)
  }
  return queue
}



/** `#rrggbb` to three numbers in 0..1. Anything unreadable is mid grey, which
    is visible and obviously wrong rather than invisible and puzzling. */
export function toRgb(hex) {
  const clean = String(hex || '').trim().replace('#', '')
  const full = clean.length === 3
    ? clean.split('').map((one) => one + one).join('')
    : clean
  if (!/^[0-9a-fA-F]{6}$/.test(full)) return [0.5, 0.5, 0.5]
  return [
    parseInt(full.slice(0, 2), 16) / 255,
    parseInt(full.slice(2, 4), 16) / 255,
    parseInt(full.slice(4, 6), 16) / 255,
  ]
}

/** The same colour with an alpha, for the settings that take a string. */
export function rgba(hex, alpha) {
  const [r, g, b] = toRgb(hex)
  return `rgba(${Math.round(r * 255)},${Math.round(g * 255)},${Math.round(b * 255)},${alpha})`
}

/**
 * Find nodes in a tree by the path that leads to them.
 *
 * The tree is built from paths and then forgets them: a node knows its own
 * label and its parent, and the whole path only exists as the walk down.
 * Which is fine until something outside has a path and wants the node --
 * which is what marking the catalogue's filter on the map needs, sixty
 * thousand times over.
 *
 * So the walk is made cheap rather than the paths stored. A parent's
 * children are turned into a label lookup the first time anybody goes
 * through it and kept, so a folder with a thousand clips under it is
 * scanned once no matter how many of them are asked for. Nothing is built
 * for a folder nobody looks in, which over three quarters of a million
 * nodes is almost all of them.
 *
 * Shelves are walked through rather than into. They are the map's own
 * invention -- ranges made to stop a folder having a thousand children --
 * and no path a vendor wrote has `Ab–Ci` in it, so a shelf's children are
 * folded into its parent's lookup as if the shelf were not there.
 * @see capFanOut
 */
export function pathFinder(tree) {
  const kids = new Map()

  const lookup = (parent) => {
    const held = kids.get(parent)
    if (held) return held

    const found = new Map()
    // Breadth-first through shelves, so a label is found at the depth the
    // catalogue put it at rather than the depth the map made for it.
    const queue = [parent]
    for (let i = 0; i < queue.length; i++) {
      const at = queue[i]
      const from = at < 0 ? 0 : tree.childAt[at]
      const to = at < 0 ? 0 : tree.childAt[at + 1]
      for (let n = from; n < to; n++) {
        const child = tree.childList[n]
        if (tree.nodes[child].shelf) { queue.push(child); continue }
        if (!found.has(tree.nodes[child].label)) found.set(tree.nodes[child].label, child)
      }
    }
    kids.set(parent, found)
    return found
  }

  // The roots, which have no parent to be looked up under.
  const roots = new Map()
  for (let at = 0; at < tree.parents.length; at++) {
    if (tree.parents[at] !== -1) continue
    if (tree.nodes[at].shelf) continue
    if (!roots.has(tree.nodes[at].label)) roots.set(tree.nodes[at].label, at)
  }
  kids.set(-1, roots)

  /** The node at this path, or -1. `path` is the labels, top level first. */
  return function find(path) {
    let at = -1
    for (const step of path) {
      const found = lookup(at).get(step)
      if (found === undefined) return -1
      at = found
    }
    return at
  }
}

/**
 * Every node the filter touches, plus the way down to each.
 *
 * A folder is lit when something under it matched, because a lit clip
 * inside a dark folder is a clip nobody can find. `pathsOf` gives each
 * row's path as an array of labels.
 */
export function markPaths(tree, rows, pathsOf) {
  const find = pathFinder(tree)
  const marked = new Set()

  for (const row of rows) {
    let at = find(pathsOf(row))
    if (at < 0) continue
    // Up to the root, stopping as soon as an ancestor is already lit: the
    // rest of that branch was lit by whatever got there first.
    while (at >= 0 && !marked.has(at)) {
      marked.add(at)
      at = tree.parents[at]
    }
  }

  return marked
}
