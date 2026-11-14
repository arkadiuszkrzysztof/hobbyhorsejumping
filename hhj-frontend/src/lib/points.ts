import type { Outcome, Run, Scored } from '../api/types'
import { formatTime } from './time'

/** Penalty points as on a result sheet, without trailing zeros: "8", "9.6", "0.25". */
export function formatPoints(points: number): string {
    return String(Math.round(points * 100) / 100)
}

/**
 * The total and, when there are time penalties, how it splits into fences +
 * time — the way LiveJumping prints it ("12  12+0"): "9.6 pts (8+1.6)".
 */
export function pointsText(score: Scored): string {
    const total = `${formatPoints(score.points)} pts`
    if (score.time_points <= 0) return total
    return `${total} (${formatPoints(score.fence_points)}+${formatPoints(score.time_points)})`
}

/** How a round that was not completed is marked on a result sheet. */
export const OUTCOME_TAG: Record<Outcome, 'RET' | 'ELI' | null> = {
    FINISHED: null,
    RETIRED: 'RET',
    ELIMINATED: 'ELI',
}

/** One run in a few characters: "24.50 · 9.6 pts (8+1.6)", "RET @7 · 8 pts", "live". */
export function runText(run: Run): string {
    if (run.status === 'OPEN') return 'live'
    const tag = OUTCOME_TAG[run.outcome]
    const head = tag === null ? formatTime(run.time_ms) : run.outcome_fence ? `${tag} @${run.outcome_fence}` : tag
    return `${head} · ${pointsText(run)}`
}

/** A typed penalty value: 0 to 99.99 with at most two decimals, comma or dot. null = not valid. */
export function parsePoints(text: string): number | null {
    const t = text.trim().replace(',', '.')
    return /^\d{1,2}(\.\d{1,2})?$/.test(t) ? Number(t) : null
}

/** A typed time allowed: whole seconds 1 to 999; empty = none (null); undefined = not valid. */
export function parseTimeAllowed(text: string): number | null | undefined {
    const t = text.trim()
    if (t === '') return null
    return /^\d{1,3}$/.test(t) && Number(t) >= 1 ? Number(t) : undefined
}
