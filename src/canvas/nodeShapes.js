/**
 * What a node on the map is drawn as.
 *
 * Its own file, with nothing imported into it, because two quite different
 * places need it: the renderer, which traces the outlines, and the control
 * panel, which offers the names in a menu. The panel is on screen from the
 * first frame and the renderer brings d3 with it, so putting the list where
 * the drawing is would have pulled a layout engine into the settings dialog
 * to fill in a dropdown. @see canvas/treeGraph.js, components/SettingsDialog.vue
 */

/**
 * The shapes a node can be drawn as, and how big each has to be to look it.
 *
 * A groove loops and a fill happens once: two different things doing two
 * different jobs, and on a map of three quarters of a million clips the only
 * way to tell them apart was to click one. So they get shapes.
 *
 * The number beside each is what its size has to be multiplied by for it to
 * cover the same area as a circle of the given radius. Without it a square
 * drawn at `r` covers four fifths again as much ink as the circle beside it
 * and reads as a bigger node -- which on a map where size means "how much is
 * under this" is not a style choice, it is a wrong number. The factors are
 * the areas solved rather than eyeballed, and given as the distance from the
 * centre to a corner, which is what the tracing below walks: a square of side
 * `s` has `s² = πr²`, so its half-diagonal is `r√(π/2)`, and so on down.
 */
const SHAPES = {
  circle: { title: 'Circle', grow: 1 },
  square: { title: 'Square', grow: 1.2533, sides: 4, turn: Math.PI / 4 },
  diamond: { title: 'Diamond', grow: 1.2533, sides: 4, turn: 0 },
  triangle: { title: 'Triangle', grow: 1.5551, sides: 3, turn: 0 },
  hexagon: { title: 'Hexagon', grow: 1.0996, sides: 6, turn: 0 },
  star: { title: 'Star', grow: 1.6727, points: 5, dent: 0.382 },
}

/** The shapes, for a menu to offer. @see components/SettingsDialog.vue */
export const NODE_SHAPES = Object.entries(SHAPES)
  .map(([value, one]) => ({ title: one.title, value }))

/**
 * One node's outline, ready to fill or stroke.
 *
 * Every shape is traced from the centre out, so the same call draws the node,
 * its halo and the ring around it at three different sizes and they stay
 * concentric. An unknown name falls back to the circle rather than drawing
 * nothing, because a map with holes in it is worse than a map that ignored a
 * setting.
 */
export function traceShape(ctx, name, x, y, r) {
  const shape = SHAPES[name] || SHAPES.circle
  const size = r * shape.grow

  if (shape.points) {
    const inner = size * shape.dent
    for (let i = 0; i < shape.points * 2; i++) {
      // Point up: the first vertex straight above the centre.
      const angle = (i * Math.PI) / shape.points - Math.PI / 2
      const reach = i % 2 ? inner : size
      const px = x + Math.cos(angle) * reach
      const py = y + Math.sin(angle) * reach
      if (i) ctx.lineTo(px, py)
      else ctx.moveTo(px, py)
    }
    ctx.closePath()
    return
  }

  if (!shape.sides) {
    ctx.arc(x, y, size, 0, Math.PI * 2)
    return
  }

  for (let i = 0; i < shape.sides; i++) {
    const angle = (i * 2 * Math.PI) / shape.sides - Math.PI / 2 + (shape.turn || 0)
    const px = x + Math.cos(angle) * size
    const py = y + Math.sin(angle) * size
    if (i) ctx.lineTo(px, py)
    else ctx.moveTo(px, py)
  }
  ctx.closePath()
}
