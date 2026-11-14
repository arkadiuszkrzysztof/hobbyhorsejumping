import { describe, expect, it } from 'vitest'
import { run } from '../test/fakes'
import { formatPoints, parsePoints, parseTimeAllowed, pointsText, runText } from './points'

describe('formatPoints', () => {
    it('drops trailing zeros and float noise', () => {
        expect(formatPoints(8)).toBe('8')
        expect(formatPoints(0)).toBe('0')
        expect(formatPoints(9.6)).toBe('9.6')
        expect(formatPoints(0.25)).toBe('0.25')
        expect(formatPoints(0.4 * 3)).toBe('1.2') // 1.2000000000000002
        expect(formatPoints(17.25)).toBe('17.25')
    })
})

describe('pointsText', () => {
    it('is the total, split into fences + time only when the time cost something', () => {
        expect(pointsText({ points: 8, fence_points: 8, time_points: 0 })).toBe('8 pts')
        expect(pointsText({ points: 0, fence_points: 0, time_points: 0 })).toBe('0 pts')
        expect(pointsText({ points: 9.6, fence_points: 8, time_points: 1.6 })).toBe('9.6 pts (8+1.6)')
        expect(pointsText({ points: 0.4, fence_points: 0, time_points: 0.4 })).toBe('0.4 pts (0+0.4)')
    })
})

describe('runText', () => {
    it('a completed run: time and points', () => {
        expect(runText(run('r', 23_450, { penalties: ['7', '9B'] }))).toBe('23.45 · 8 pts')
        expect(runText(run('r', 24_500, { penalties: ['1', '2'], time_points: 1.6 }))).toBe('24.50 · 9.6 pts (8+1.6)')
        expect(runText(run('r', null))).toBe('— · 0 pts')
    })

    it('a round that was not completed: RET / ELI, where it ended if known, and the points so far', () => {
        expect(runText(run('r', null, { outcome: 'RETIRED', outcome_fence: '7', penalties: ['2', '5'] }))).toBe('RET @7 · 8 pts')
        expect(runText(run('r', null, { outcome: 'RETIRED' }))).toBe('RET · 0 pts')
        expect(runText(run('r', null, { outcome: 'ELIMINATED', outcome_fence: '9B', refusals: { '9': 1 } }))).toBe('ELI @9B · 4 pts')
    })

    it('the open one is live', () => {
        expect(runText(run('r', 5_000, { status: 'OPEN' }))).toBe('live')
    })
})

describe('parsePoints', () => {
    it('accepts 0 to 99.99 with at most two decimals, comma or dot', () => {
        expect(parsePoints('4')).toBe(4)
        expect(parsePoints(' 0.4 ')).toBe(0.4)
        expect(parsePoints('0,4')).toBe(0.4)
        expect(parsePoints('0')).toBe(0)
        expect(parsePoints('99.99')).toBe(99.99)
        expect(parsePoints('2.50')).toBe(2.5)
    })

    it('rejects everything else', () => {
        for (const bad of ['', ' ', '-1', '100', '0.123', '.5', '4.', 'four', '1e1', '1 2', '4,5,6', '+4']) {
            expect(parsePoints(bad), bad).toBeNull()
        }
    })
})

describe('parseTimeAllowed', () => {
    it('empty means none', () => {
        expect(parseTimeAllowed('')).toBeNull()
        expect(parseTimeAllowed('   ')).toBeNull()
    })

    it('whole seconds from 1 to 999', () => {
        expect(parseTimeAllowed('45')).toBe(45)
        expect(parseTimeAllowed(' 1 ')).toBe(1)
        expect(parseTimeAllowed('999')).toBe(999)
    })

    it('anything else is not valid', () => {
        for (const bad of ['0', '1000', '45.5', '45,5', '-3', 'x', '4 5']) {
            expect(parseTimeAllowed(bad), bad).toBeUndefined()
        }
    })
})
