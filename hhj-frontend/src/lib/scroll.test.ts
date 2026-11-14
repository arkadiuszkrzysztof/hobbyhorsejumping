import { describe, expect, it } from 'vitest'
import { centredScrollTop } from './scroll'

// a list 600 px high showing 3000 px of rows, each 80 px
const view = (coveredBottom = 0) => ({ height: 600, coveredBottom, scrollHeight: 3000 })

describe('centredScrollTop', () => {
    it('puts the row in the middle of the list', () => {
        // row at 1000..1080: its centre (1040) goes to the centre of the view (300)
        expect(centredScrollTop({ top: 1000, height: 80 }, view())).toBe(740)
    })

    it('uses the middle of the part that is not covered by a bar at the bottom', () => {
        // 100 px covered: the visible part is 500 px, its centre at 250
        expect(centredScrollTop({ top: 1000, height: 80 }, view(100))).toBe(790)
    })

    it('stops at the ends of the list', () => {
        expect(centredScrollTop({ top: 0, height: 80 }, view())).toBe(0)
        expect(centredScrollTop({ top: 100, height: 80 }, view())).toBe(0)
        expect(centredScrollTop({ top: 2920, height: 80 }, view())).toBe(2400)
        expect(centredScrollTop({ top: 2700, height: 80 }, view(100))).toBe(2400)
    })

    it('a list that fits does not scroll', () => {
        expect(centredScrollTop({ top: 300, height: 80 }, { height: 600, coveredBottom: 100, scrollHeight: 600 })).toBe(0)
    })

    it('copes with a bar that covers everything', () => {
        expect(centredScrollTop({ top: 1000, height: 80 }, { height: 100, coveredBottom: 300, scrollHeight: 3000 })).toBe(1040)
    })
})
