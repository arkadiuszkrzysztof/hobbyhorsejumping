import type { Session } from '../api/types'

const pad2 = (n: number) => String(n).padStart(2, '0')

/**
 * Race time. Truncated to hundredths (never rounded up, like the display).
 * Under 100 s: "S.hh" / "SS.hh"; from 100 s: "M:SS.hh". null → "—".
 */
export function formatTime(ms: number | null | undefined): string {
    if (ms === null || ms === undefined || !Number.isFinite(ms)) return '—'
    const sign = ms < 0 ? '-' : ''
    const hundredths = Math.floor(Math.abs(ms) / 10)
    const hh = pad2(hundredths % 100)
    const totalSeconds = Math.floor(hundredths / 100)
    if (totalSeconds < 100) return `${sign}${totalSeconds}.${hh}`
    const minutes = Math.floor(totalSeconds / 60)
    return `${sign}${minutes}:${pad2(totalSeconds % 60)}.${hh}`
}

/** Local wall-clock time of an epoch-ms timestamp: "HH:MM:SS.hh" (truncated). */
export function formatTimeOfDay(epochMs: number): string {
    const d = new Date(epochMs)
    const hh = Math.floor(d.getMilliseconds() / 10)
    return `${pad2(d.getHours())}:${pad2(d.getMinutes())}:${pad2(d.getSeconds())}.${pad2(hh)}`
}

/**
 * Server-clock estimate. API.md: keep the largest (server_time - local now)
 * ever seen — network delay only ever makes the observed offset smaller.
 */
export function nextClockOffset(prev: number | null, serverTime: number, localNow: number): number {
    const observed = serverTime - localNow
    return prev === null ? observed : Math.max(prev, observed)
}

export function estimateServerNow(offset: number | null, localNow: number): number {
    return localNow + (offset ?? 0)
}

export type ReadoutMode = 'none' | 'zero' | 'running' | 'result'

export interface Readout {
    mode: ReadoutMode
    ms: number
}

/**
 * What the big time readout shows (mirrors the external display):
 * no counting START → 0.00; counting START without counting FINISH → running
 * clock; time_ms set → the result.
 */
export function computeReadout(session: Session | null, serverNow: number): Readout {
    if (!session) return { mode: 'none', ms: 0 }
    if (session.time_ms !== null) return { mode: 'result', ms: session.time_ms }
    const start = session.start_id ? session.starts.find((s) => s.id === session.start_id) : undefined
    if (!start || session.finish_id !== null) return { mode: 'zero', ms: 0 }
    return { mode: 'running', ms: Math.max(0, serverNow - start.sensor_time) }
}
