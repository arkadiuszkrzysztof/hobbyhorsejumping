import type { Entry, Run } from '../api/types'
import { OUTCOME_TAG } from './points'

/** What a completed, timed run is worth: penalty points and the time as shown, both in hundredths. */
export interface Result {
    run: Run
    points: number
    hundredths: number
}

/**
 * Negative when `a` is the better result: fewer penalty points first, then the
 * shorter time — a clean 30 s beats 20 s with one knockdown.
 */
export function compareResults(a: Result, b: Result): number {
    return a.points - b.points || a.hundredths - b.hundredths
}

/** The run that counts for a competitor: the best of the saved, completed runs that have a time. */
export function bestResult(entry: Entry): Result | null {
    let best: Result | null = null
    for (const run of entry.runs) {
        if (run.status !== 'SAVED' || run.outcome !== 'FINISHED' || run.time_ms === null) continue
        // In hundredths, like everything on screen, so equal-looking results rank equal.
        const result = { run, points: Math.round(run.points * 100), hundredths: Math.floor(run.time_ms / 10) }
        if (best === null || compareResults(result, best) < 0) best = result
    }
    return best
}

/** For a competitor without a result: RET / ELI if their last saved round was not completed. */
export function unfinishedMark(entry: Entry): 'RET' | 'ELI' | null {
    const saved = entry.runs.filter((run) => run.status === 'SAVED')
    const last = saved[saved.length - 1]
    return last ? OUTCOME_TAG[last.outcome] : null
}

export interface Ranked {
    entry: Entry
    /** 1 = winner; equal results share a place (1, 2, 2, 4); null = no result. */
    place: number | null
    result: Result | null
    /** Without a result: retired or eliminated, else null (has not ridden, or no time). */
    mark: 'RET' | 'ELI' | null
}

/**
 * Competitors with a result, best first (ties keep their start order); then
 * those who retired or were eliminated; then the others — each group in start order.
 */
export function ranking(entries: Entry[]): Ranked[] {
    const all = entries.map((entry) => ({ entry, result: bestResult(entry) }))
    const scored = all.flatMap((r) => (r.result ? [{ entry: r.entry, result: r.result }] : []))
    scored.sort((a, b) => compareResults(a.result, b.result)) // stable
    let place = 0
    let previous: Result | null = null
    const ranked: Ranked[] = scored.map((r, i) => {
        if (previous === null || compareResults(previous, r.result) !== 0) place = i + 1
        previous = r.result
        return { ...r, place, mark: null }
    })
    const rest: Ranked[] = all
        .filter((r) => r.result === null)
        .map((r) => ({ entry: r.entry, place: null, result: null, mark: unfinishedMark(r.entry) }))
    return [...ranked, ...rest.filter((r) => r.mark !== null), ...rest.filter((r) => r.mark === null)]
}
