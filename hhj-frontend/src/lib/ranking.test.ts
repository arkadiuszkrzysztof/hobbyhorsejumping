import { describe, expect, it } from 'vitest'
import type { Run } from '../api/types'
import { entry } from '../test/fakes'
import { bestResult, compareResults, ranking } from './ranking'

let nextId = 1
const saved = (time_ms: number | null, penalties: string[] = []): Run => ({
    id: `r${nextId++}`,
    number: 1,
    status: 'SAVED',
    time_ms,
    penalties,
})
const open = (): Run => ({ id: `r${nextId++}`, number: 9, status: 'OPEN', time_ms: 5_000, penalties: [] })
const result = (time_ms: number, points: number) => ({
    run: saved(time_ms),
    points,
    hundredths: Math.floor(time_ms / 10),
})

describe('compareResults', () => {
    it('a clean 30 s beats 20 s with one penalty', () => {
        expect(compareResults(result(30_000, 0), result(20_000, 1))).toBeLessThan(0)
        expect(compareResults(result(20_000, 1), result(30_000, 0))).toBeGreaterThan(0)
    })

    it('with equal penalties the shorter time wins', () => {
        expect(compareResults(result(20_000, 2), result(20_010, 2))).toBeLessThan(0)
        expect(compareResults(result(20_010, 2), result(20_000, 2))).toBeGreaterThan(0)
    })

    it('times that show the same hundredths are equal', () => {
        expect(compareResults(result(23_451, 0), result(23_459, 0))).toBe(0)
        expect(compareResults(result(23_459, 0), result(23_460, 0))).toBeLessThan(0)
    })
})

describe('bestResult', () => {
    it('is null without a saved, timed run', () => {
        expect(bestResult(entry('e', 'A'))).toBeNull()
        expect(bestResult(entry('e', 'A', [open()]))).toBeNull()
        expect(bestResult(entry('e', 'A', [saved(null, ['3'])]))).toBeNull()
    })

    it('counts every marked fence element as one point', () => {
        const best = bestResult(entry('e', 'A', [saved(21_000, ['7', '9B', '9C'])]))
        expect(best).toMatchObject({ points: 3, hundredths: 2100 })
    })

    it('picks the run with the fewest penalties, then the fastest', () => {
        const fastButFaulty = saved(18_000, ['4'])
        const clean = saved(27_000)
        const cleanAndFaster = saved(25_500)
        const e = entry('e', 'A', [fastButFaulty, clean, cleanAndFaster, open(), saved(null)])
        expect(bestResult(e)?.run).toBe(cleanAndFaster)
    })

    it('keeps the earlier of two equal runs', () => {
        const first = saved(20_001)
        const second = saved(20_009)
        expect(bestResult(entry('e', 'A', [first, second]))?.run).toBe(first)
    })
})

describe('ranking', () => {
    it('orders by penalties, then time; competitors without a result follow in start order', () => {
        const entries = [
            entry('e1', 'Waiting'),
            entry('e2', 'Fast with a fault', [saved(20_000, ['5'])]),
            entry('e3', 'Slow and clean', [saved(30_000)]),
            entry('e4', 'Riding now', [open()]),
            entry('e5', 'Quick and clean', [saved(24_000)]),
            entry('e6', 'Two faults', [saved(15_000, ['1', '2A'])]),
        ]
        const rows = ranking(entries)
        expect(rows.map((r) => [r.place, r.entry.name])).toEqual([
            [1, 'Quick and clean'],
            [2, 'Slow and clean'],
            [3, 'Fast with a fault'],
            [4, 'Two faults'],
            [null, 'Waiting'],
            [null, 'Riding now'],
        ])
        expect(rows[4]!.result).toBeNull()
        expect(rows[0]!.result?.run).toBe(entries[4]!.runs[0])
    })

    it('uses the best run of each competitor', () => {
        const rows = ranking([
            entry('e1', 'One try', [saved(22_000)]),
            entry('e2', 'Second try better', [saved(21_000, ['3']), saved(21_500)]),
        ])
        expect(rows.map((r) => r.entry.name)).toEqual(['Second try better', 'One try'])
    })

    it('equal results share a place and keep their start order', () => {
        const rows = ranking([
            entry('e1', 'A', [saved(25_000)]),
            entry('e2', 'B', [saved(20_004)]),
            entry('e3', 'C', [saved(20_001)]),
            entry('e4', 'D', [saved(26_000)]),
        ])
        expect(rows.map((r) => [r.place, r.entry.name])).toEqual([
            [1, 'B'],
            [1, 'C'],
            [3, 'A'],
            [4, 'D'],
        ])
    })

    it('is empty for an empty list and does not reorder its input', () => {
        expect(ranking([])).toEqual([])
        const entries = [entry('e1', 'A', [saved(30_000)]), entry('e2', 'B', [saved(20_000)])]
        ranking(entries)
        expect(entries.map((e) => e.id)).toEqual(['e1', 'e2'])
    })
})
