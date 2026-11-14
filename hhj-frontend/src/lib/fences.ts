/** Fence numbers split into two rows; the first row gets the ceiling half. */
export function fenceRows(count: number): number[][] {
    const n = Math.max(0, Math.floor(count))
    const firstLen = Math.ceil(n / 2)
    const all = Array.from({ length: n }, (_, i) => i + 1)
    return [all.slice(0, firstLen), all.slice(firstLen)].filter((row) => row.length > 0)
}

export const FENCE_PARTS = ['A', 'B', 'C'] as const

/** Sort penalty labels naturally: 2, 7, 9B, 10, 10A. */
export function sortPenalties(labels: readonly string[]): string[] {
    return [...labels].sort((a, b) => a.localeCompare(b, 'en', { numeric: true }))
}
