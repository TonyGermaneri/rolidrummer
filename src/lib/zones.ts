import type { Zone } from '../model'
export const maxZones = 8
export const minZoneWidth = .01
export function setBoundary(zones: Zone[], index: number, value: number) {
 if (index < 0 || index >= zones.length - 1 || !Number.isFinite(value)) return
 const low = index ? zones[index - 1].upper : 0, high = zones[index + 1].upper
 zones[index].upper = Math.round(Math.max(low + minZoneWidth, Math.min(high - minZoneWidth, value)) * 1000) / 1000
}
export function splitZone(zones: Zone[], index: number) {
 const zone = zones[index], low = index ? zones[index - 1]?.upper : 0
 if (!zone || zones.length >= maxZones || zone.upper - low < minZoneWidth * 2) return false
 zones.splice(index, 0, { upper: (low + zone.upper) / 2, clipId: zone.clipId })
 return true
}
export function removeZone(zones: Zone[], index: number) {
 if (zones.length < 2 || !zones[index]) return false
 zones.splice(index, 1)
 zones[zones.length - 1].upper = 1
 return true
}
