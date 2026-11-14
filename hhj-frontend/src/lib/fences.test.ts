import { describe, expect, it } from 'vitest'
import { fenceRows, sortPenalties } from './fences'

const sizes = (n: number) => fenceRows(n).map((r) => r.length)

describe('fenceRows', () => {
    it('12 → 6 + 6', () => {
        expect(fenceRows(12)).toEqual([
            [1, 2, 3, 4, 5, 6],
            [7, 8, 9, 10, 11, 12],
        ])
    })
    it('18 → 9 + 9', () => expect(sizes(18)).toEqual([9, 9]))
    it('odd counts give the first row the ceiling half', () => {
        expect(sizes(13)).toEqual([7, 6])
        expect(sizes(3)).toEqual([2, 1])
        expect(fenceRows(5)).toEqual([[1, 2, 3], [4, 5]])
    })
    it('1 → a single row', () => expect(fenceRows(1)).toEqual([[1]]))
    it('24 → 12 + 12', () => expect(sizes(24)).toEqual([12, 12]))
})

describe('sortPenalties', () => {
    it('sorts naturally', () => {
        expect(sortPenalties(['10A', '9B', '2', '10', '7'])).toEqual(['2', '7', '9B', '10', '10A'])
    })
})
