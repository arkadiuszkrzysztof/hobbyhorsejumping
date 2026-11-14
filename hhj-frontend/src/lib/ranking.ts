import type { Entry, Run } from '../api/types'

/** What a run is worth: one penalty point per marked fence element, and the time as shown (hundredths). */
export interface Result {
    run: Run
    points: number
    hundredths: number
}

/**
 * Negative when `a` is the better result: fewer penalty points first, then the
 * shorter time — a clean 30 s beats 20 s with one penalty.
 */
export function compareResults(a: Result, b: Result): number {
    return a.points - b.points || a.hundredths - b.hundredths
}

/** The run that counts for a competitor: the best of the saved runs that have a time. */
export function bestResult(entry: Entry): Result | null {
    let best: Result | null = null
    for (const run of entry.runs) {
        if (run.status !== 'SAVED' || run.time_ms === null) continue
        // Truncated like every time on screen, so equal-looking times rank equal.
        const result = { run, points: run.penalties.length, hundredths: Math.floor(run.time_ms / 10) }
        if (best === null || compareResults(result, best) < 0) best = result
    }
    return best
}

export interface Ranked {
    entry: Entry
    /** 1 = winner; equal results share a place (1, 2, 2, 4); null = no result yet. */
    place: number | null
    result: Result | null
}

/** Competitors with a result, best first (ties keep their start order), then the others in start order. */
export function ranking(entries: Entry[]): Ranked[] {
    const all = entries.map((entry) => ({ entry, result: bestResult(entry) }))
    const scored = all.flatMap((r) => (r.result ? [{ entry: r.entry, result: r.result }] : []))
    scored.sort((a, b) => compareResults(a.result, b.result)) // stable
    let place = 0
    let previous: Result | null = null
    const ranked: Ranked[] = scored.map((r, i) => {
        if (previous === null || compareResults(previous, r.result) !== 0) place = i + 1
        previous = r.result
        return { ...r, place }
    })
    const unranked: Ranked[] = all.filter((r) => r.result === null).map((r) => ({ ...r, place: null }))
    return [...ranked, ...unranked]
}
