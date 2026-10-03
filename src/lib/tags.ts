/** Tags are stored as one comma-separated string per pad ("drums, 808, dirt-samples"). */

export const splitTags = (tag: string | undefined): string[] => {
  const seen = new Set<string>()
  const out: string[] = []
  for (const t of (tag ?? '').split(',')) {
    const name = t.trim()
    if (name && !seen.has(name.toLowerCase())) {
      seen.add(name.toLowerCase())
      out.push(name)
    }
  }
  return out
}

export const joinTags = (tags: string[]) => splitTags(tags.join(',')).join(', ')

/** Add tags to a tag string, keeping existing ones first. */
export const addTags = (tag: string, extra: string[]) => joinTags([...splitTags(tag), ...extra])

/** Does a pad carry every selected tag? (case-insensitive) */
export function hasAllTags(tag: string, selected: string[]): boolean {
  if (!selected.length) return true
  const mine = new Set(splitTags(tag).map((t) => t.toLowerCase()))
  return selected.every((t) => mine.has(t.toLowerCase()))
}

/** Does a pad carry none of the hidden tags? (case-insensitive) */
export function hasNoTags(tag: string, hidden: string[]): boolean {
  if (!hidden.length) return true
  const mine = new Set(splitTags(tag).map((t) => t.toLowerCase()))
  return !hidden.some((t) => mine.has(t.toLowerCase()))
}

/** Shown by the tag filter: every selected tag, none of the hidden ones. */
export const passesTags = (tag: string, selected: string[], hidden: string[] = []) =>
  hasAllTags(tag, selected) && hasNoTags(tag, hidden)

export interface Facet {
  name: string
  count: number
  selected: boolean
  /** hidden (right-click): pads carrying it are filtered out; count = how many it hides */
  excluded: boolean
}

/**
 * Tag facets for the pads left after filtering: each tag with how many of those pads carry it.
 * Hidden tags stay listed (so they can be shown again), counting the pads they hide.
 * Selected tags come first, then hidden ones; the rest by count, then name.
 */
export function tagFacets(tagStrings: string[], selected: string[], hidden: string[] = []): Facet[] {
  const sel = new Set(selected.map((t) => t.toLowerCase()))
  const hid = new Set(hidden.map((t) => t.toLowerCase()))
  const counts = new Map<string, Facet>()
  const bump = (t: string, excluded: boolean) => {
    const key = t.toLowerCase()
    const f = counts.get(key)
    if (f) f.count++
    else counts.set(key, { name: t, count: 1, selected: sel.has(key), excluded })
  }
  for (const s of tagStrings) {
    if (!hasAllTags(s, selected)) continue
    const tags = splitTags(s)
    if (hasNoTags(s, hidden)) for (const t of tags) bump(t, hid.has(t.toLowerCase()))
    else for (const t of tags) if (hid.has(t.toLowerCase())) bump(t, true)
  }
  // a hidden tag nothing carries any more still gets its chip back
  for (const t of hidden) if (!counts.has(t.toLowerCase())) counts.set(t.toLowerCase(), { name: t, count: 0, selected: false, excluded: true })
  return [...counts.values()].sort(
    (a, b) =>
      Number(b.selected) - Number(a.selected) ||
      Number(b.excluded) - Number(a.excluded) ||
      b.count - a.count ||
      a.name.localeCompare(b.name),
  )
}
