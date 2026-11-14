import { describe, expect, it } from 'vitest'
import type { Run } from '../api/types'
import { entry, run } from '../test/fakes'
import { bestResult, compareResults, ranking, unfinishedMark } from './ranking'

let nextId = 1
/** A saved, completed run with that many knockdowns (4 points each). */
const saved = (time_ms: number | null, knockdowns = 0, patch: Partial<Run> = {}): Run =>
    run(`r${nextId++}`, time_ms, { penalties: Array.from({ length: knockdowns }, (_, i) => String(i + 1)), ...patch })
const open = (): Run => run(`r${nextId++}`, 5_000, { status: 'OPEN', number: 9 })
const retired = (fence = ''): Run => saved(null, 1, { outcome: 'RETIRED', outcome_fence: fence })
const eliminated = (): Run => saved(null, 0, { outcome: 'ELIMINATED' })
const result = (time_ms: number, points: number) => ({
    run: saved(time_ms),
    points: Math.round(points * 100),
    hundredths: Math.floor(time_ms / 10),
})

describe('compareResults', () => {
    it('a clean 30 s beats 20 s with one knockdown', () => {
        expect(compareResults(result(30_000, 0), result(20_000, 4))).toBeLessThan(0)
        expect(compareResults(result(20_000, 4), result(30_000, 0))).toBeGreaterThan(0)
    })

    it('fewer points win, whatever they are for', () => {
        expect(compareResults(result(40_000, 1.6), result(20_000, 4))).toBeLessThan(0) // time penalties only
        expect(compareResults(result(40_000, 9.6), result(20_000, 9.2))).toBeGreaterThan(0)
    })

    it('with equal points the shorter time wins', () => {
        expect(compareResults(result(20_000, 8), result(20_010, 8))).toBeLessThan(0)
        expect(compareResults(result(20_010, 8), result(20_000, 8))).toBeGreaterThan(0)
    })

    it('times that show the same hundredths are equal', () => {
        expect(compareResults(result(23_451, 0), result(23_459, 0))).toBe(0)
        expect(compareResults(result(23_459, 0), result(23_460, 0))).toBeLessThan(0)
    })
})

describe('bestResult', () => {
    it('is null without a saved, completed, timed run', () => {
        expect(bestResult(entry('e', 'A'))).toBeNull()
        expect(bestResult(entry('e', 'A', [open()]))).toBeNull()
        expect(bestResult(entry('e', 'A', [saved(null, 1)]))).toBeNull()
        expect(bestResult(entry('e', 'A', [retired(), eliminated()]))).toBeNull()
        // a retired round does not count even if the server still sent a time with it
        expect(bestResult(entry('e', 'A', [saved(20_000, 0, { outcome: 'RETIRED' })]))).toBeNull()
    })

    it('takes the points as the server scored them', () => {
        const best = bestResult(entry('e', 'A', [saved(21_000, 2, { time_points: 1.6 })]))
        expect(best).toMatchObject({ points: 960, hundredths: 2100 })
    })

    it('picks the run with the fewest points, then the fastest', () => {
        const fastButFaulty = saved(18_000, 1)
        const clean = saved(27_000)
        const cleanAndFaster = saved(25_500)
        const e = entry('e', 'A', [fastButFaulty, clean, cleanAndFaster, open(), saved(null), retired()])
        expect(bestResult(e)?.run).toBe(cleanAndFaster)
    })

    it('keeps the earlier of two equal runs', () => {
        const first = saved(20_001)
        const second = saved(20_009)
        expect(bestResult(entry('e', 'A', [first, second]))?.run).toBe(first)
    })
})

describe('unfinishedMark', () => {
    it('is RET / ELI when the last saved round was not completed', () => {
        expect(unfinishedMark(entry('e', 'A', [retired('7')]))).toBe('RET')
        expect(unfinishedMark(entry('e', 'A', [retired(), eliminated()]))).toBe('ELI')
        expect(unfinishedMark(entry('e', 'A', [eliminated(), open()]))).toBe('ELI') // the open one is not saved yet
    })

    it('is null otherwise', () => {
        expect(unfinishedMark(entry('e', 'A'))).toBeNull()
        expect(unfinishedMark(entry('e', 'A', [open()]))).toBeNull()
        expect(unfinishedMark(entry('e', 'A', [retired(), saved(null)]))).toBeNull()
    })
})

describe('ranking', () => {
    it('orders by points, then time; then retired / eliminated; then the rest — each in start order', () => {
        const entries = [
            entry('e1', 'Waiting'),
            entry('e2', 'Fast with a fault', [saved(20_000, 1)]),
            entry('e3', 'Gave up', [retired('4')]),
            entry('e4', 'Slow and clean', [saved(30_000)]),
            entry('e5', 'Riding now', [open()]),
            entry('e6', 'Quick and clean', [saved(24_000)]),
            entry('e7', 'Fell off', [eliminated()]),
            entry('e8', 'Two faults', [saved(15_000, 2)]),
            entry('e9', 'Clean but slow: time penalties', [saved(40_000, 0, { time_points: 2 })]),
        ]
        const rows = ranking(entries)
        expect(rows.map((r) => [r.place ?? r.mark, r.entry.name])).toEqual([
            [1, 'Quick and clean'],
            [2, 'Slow and clean'],
            [3, 'Clean but slow: time penalties'],
            [4, 'Fast with a fault'],
            [5, 'Two faults'],
            ['RET', 'Gave up'],
            ['ELI', 'Fell off'],
            [null, 'Waiting'],
            [null, 'Riding now'],
        ])
        expect(rows[7]!.result).toBeNull()
        expect(rows[0]!.result?.run).toBe(entries[5]!.runs[0])
    })

    it('uses the best run of each competitor, and a result beats an earlier or later retirement', () => {
        const rows = ranking([
            entry('e1', 'One try', [saved(22_000)]),
            entry('e2', 'Second try better', [saved(21_000, 1), saved(21_500)]),
            entry('e3', 'Retired, then finished', [retired(), saved(50_000, 3)]),
            entry('e4', 'Finished, then retired', [saved(60_000, 3), retired()]),
        ])
        expect(rows.map((r) => [r.place, r.mark, r.entry.name])).toEqual([
            [1, null, 'Second try better'],
            [2, null, 'One try'],
            [3, null, 'Retired, then finished'],
            [4, null, 'Finished, then retired'],
        ])
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
