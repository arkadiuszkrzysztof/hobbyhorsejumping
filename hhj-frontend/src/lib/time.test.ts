import { describe, expect, it } from 'vitest'
import { session } from '../test/fakes'
import { computeReadout, estimateServerNow, formatTime, formatTimeOfDay, nextClockOffset } from './time'

describe('formatTime', () => {
    it.each([
        [0, '0.00'],
        [9, '0.00'],
        [10, '0.01'],
        [9_999, '9.99'],
        [23_450, '23.45'],
        [23_459, '23.45'],
        [99_990, '99.99'],
        [99_999, '99.99'],
        [100_000, '1:40.00'],
        [125_678, '2:05.67'],
        [5_999_999, '99:59.99'],
    ])('%i ms → %s (truncated, never rounded up)', (ms, text) => {
        expect(formatTime(ms)).toBe(text)
    })

    it('null / undefined → dash', () => {
        expect(formatTime(null)).toBe('—')
        expect(formatTime(undefined)).toBe('—')
    })
})

describe('formatTimeOfDay', () => {
    it('formats local wall-clock time with truncated hundredths', () => {
        const t = new Date(2026, 10, 14, 9, 5, 7, 999).getTime()
        expect(formatTimeOfDay(t)).toBe('09:05:07.99')
    })

    it('pads everything', () => {
        expect(formatTimeOfDay(new Date(2026, 0, 1, 0, 0, 0, 5).getTime())).toBe('00:00:00.00')
        expect(formatTimeOfDay(new Date(2026, 0, 1, 23, 59, 59, 120).getTime())).toBe('23:59:59.12')
    })
})

describe('server clock estimate', () => {
    it('keeps the largest observed offset', () => {
        let off = nextClockOffset(null, 10_500, 10_000)
        expect(off).toBe(500)
        off = nextClockOffset(off, 11_300, 11_000) // delayed message: smaller offset is ignored
        expect(off).toBe(500)
        off = nextClockOffset(off, 12_800, 12_000)
        expect(off).toBe(800)
    })

    it('estimates server now from local now', () => {
        expect(estimateServerNow(800, 20_000)).toBe(20_800)
        expect(estimateServerNow(null, 20_000)).toBe(20_000)
    })
})

describe('computeReadout', () => {
    const start = { id: 's1', sensor_time: 50_000, in_session: true }

    it('no session → none, 0', () => {
        expect(computeReadout(null, 99_999)).toEqual({ mode: 'none', ms: 0 })
    })

    it('session without counting START → 0.00', () => {
        expect(computeReadout(session(), 60_000)).toEqual({ mode: 'zero', ms: 0 })
    })

    it('counting START, no FINISH → running from sensor_time on the server clock', () => {
        const s = session({ starts: [start], start_id: 's1' })
        expect(computeReadout(s, 73_456)).toEqual({ mode: 'running', ms: 23_456 })
        expect(formatTime(computeReadout(s, 73_456).ms)).toBe('23.45')
    })

    it('never negative when local clock lags', () => {
        const s = session({ starts: [start], start_id: 's1' })
        expect(computeReadout(s, 49_000).ms).toBe(0)
    })

    it('time_ms set → result', () => {
        const s = session({
            starts: [start],
            start_id: 's1',
            finishes: [{ id: 'f1', sensor_time: 73_450, in_session: true, time_ms: 23_450 }],
            finish_id: 'f1',
            time_ms: 23_450,
        })
        expect(computeReadout(s, 999_999)).toEqual({ mode: 'result', ms: 23_450 })
    })
})
