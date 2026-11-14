import { render, screen, waitFor, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { describe, expect, it } from 'vitest'
import type { ArenaState } from './api/types'
import { App } from './App'
import { CompetitionStore, ORDER_KEY } from './state/store'
import { CID, FakeApi, RULES, apiWith, entry, makeDeps, makeState, run, session } from './test/fakes'

async function renderApp(api: FakeApi) {
    const deps = makeDeps(api)
    const store = new CompetitionStore(deps)
    const user = userEvent.setup()
    render(<App store={store} />)
    await screen.findByTestId('current-name')
    return { store, deps, user }
}

const ok = (state: ArenaState) => ({ body: state })
const later = (patch: Partial<ArenaState>) => makeState({ server_time: 1_800_000_000_500, ...patch })
const competitorRows = () =>
    within(screen.getByRole('complementary', { name: 'Competitors' })).getAllByRole('listitem').filter((li) =>
        li.classList.contains('entry')
    )

describe('competitor list', () => {
    it('renders entries in order with their run chips', async () => {
        const state = makeState({
            entries: [
                entry('e1', 'Zosia', [
                    run('r1', 23_450, { penalties: ['7', '9B'] }),
                    run('r2', null, { number: 2 }),
                ]),
                entry('e2', 'Kuba', [run('r3', null, { status: 'OPEN' })]),
                entry('e3', 'Ola'),
            ],
            session: session({ run_id: 'r3', entry_id: 'e2' }),
            selected_entry: 'e2',
        })
        await renderApp(apiWith(state))
        const rows = competitorRows()
        expect(rows.map((r) => r.querySelector('.entry-name')!.textContent)).toEqual(['Zosia', 'KubaSELECTED', 'Ola'])
        expect(rows.map((r) => r.querySelector('.entry-pos')!.textContent)).toEqual(['1', '2', '3'])
        expect(within(rows[0]!).getByText('23.45 · 8 pts')).toBeInTheDocument()
        expect(within(rows[0]!).getByText('— · 0 pts')).toBeInTheDocument()
        expect(within(rows[1]!).getByText(/live/)).toBeInTheDocument()
        expect(rows[1]).toHaveClass('entry-selected')
    })

    it('clicking a row selects it', async () => {
        const api = apiWith(makeState()).on('POST', `/api/competitions/${CID}/select/`, ok(later({ selected_entry: 'e2' })))
        const { user } = await renderApp(api)
        await user.click(screen.getByRole('button', { name: 'Kuba' }))
        expect(api.callsTo('POST', `/api/competitions/${CID}/select/`).map((c) => c.body)).toEqual([{ entry: 'e2' }])
        await waitFor(() => expect(screen.getByTestId('current-name')).toHaveTextContent('Kuba'))
    })

    it('409 on select asks to move the open session, then repeats with move', async () => {
        const state = makeState({ session: session({ entry_id: 'e1' }) })
        const api = apiWith(state).on('POST', `/api/competitions/${CID}/select/`, (call) =>
            (call.body as { move?: boolean }).move
                ? ok(later({ selected_entry: 'e2', session: session({ entry_id: 'e2' }) }))
                : { status: 409, body: { error: 'A session is open for Zosia.' } }
        )
        const { user } = await renderApp(api)
        await user.click(screen.getByRole('button', { name: 'Kuba' }))
        const dialog = await screen.findByRole('dialog')
        expect(dialog).toHaveTextContent('Session open for Zosia — move it to Kuba?')
        await user.click(within(dialog).getByRole('button', { name: 'Move to Kuba' }))
        await waitFor(() =>
            expect(api.callsTo('POST', `/api/competitions/${CID}/select/`).map((c) => c.body)).toEqual([
                { entry: 'e2' },
                { entry: 'e2', move: true },
            ])
        )
        await waitFor(() => expect(screen.getByTestId('current-name')).toHaveTextContent('Kuba'))
    })

    it('declining the move does nothing more', async () => {
        const api = apiWith(makeState({ session: session({ entry_id: 'e1' }) })).on(
            'POST',
            `/api/competitions/${CID}/select/`,
            { status: 409, body: { error: 'busy' } }
        )
        const { user } = await renderApp(api)
        await user.click(screen.getByRole('button', { name: 'Kuba' }))
        await user.click(within(await screen.findByRole('dialog')).getByRole('button', { name: 'Cancel' }))
        expect(api.callsTo('POST', `/api/competitions/${CID}/select/`)).toHaveLength(1)
        expect(screen.queryByRole('dialog')).not.toBeInTheDocument()
    })

    it('adding competitors keeps focus in the input', async () => {
        let names = ['Zosia', 'Kuba', 'Ola']
        const api = apiWith(makeState()).on('POST', `/api/competitions/${CID}/entries/`, (call) => {
            names = [...names, (call.body as { name: string }).name]
            return ok(later({ server_time: 1_800_000_000_000 + names.length, entries: names.map((n, i) => entry(`e${i + 1}`, n)) }))
        })
        const { user } = await renderApp(api)
        const input = screen.getByRole('textbox', { name: 'New competitor name' })
        await user.click(input)
        await user.type(input, 'Antek{Enter}')
        await waitFor(() => expect(input).toHaveValue(''))
        expect(input).toHaveFocus()
        await user.type(input, 'Basia{Enter}')
        await waitFor(() => expect(competitorRows()).toHaveLength(5))
        expect(input).toHaveFocus()
        expect(api.callsTo('POST', `/api/competitions/${CID}/entries/`).map((c) => c.body)).toEqual([
            { name: 'Antek' },
            { name: 'Basia' },
        ])
    })

    it('up / down buttons send the new order', async () => {
        const api = apiWith(makeState()).on('POST', `/api/competitions/${CID}/order/`, ok(later({})))
        const { user } = await renderApp(api)
        await user.click(screen.getByRole('button', { name: 'Move Ola up' }))
        expect(api.callsTo('POST', `/api/competitions/${CID}/order/`)[0]?.body).toEqual({ entries: ['e1', 'e3', 'e2'] })
        expect(screen.getByRole('button', { name: 'Move Zosia up' })).toBeDisabled()
    })

    it('removing warns about saved runs', async () => {
        const state = makeState({
            entries: [entry('e1', 'Zosia', [run('r1', 1)])],
        })
        const api = apiWith(state).on('DELETE', '/api/entries/e1/', ok(later({ entries: [] })))
        const { user } = await renderApp(api)
        await user.click(screen.getByRole('button', { name: 'Remove Zosia' }))
        const dialog = await screen.findByRole('dialog')
        expect(dialog).toHaveTextContent('1 saved run will be deleted with Zosia')
        await user.click(within(dialog).getByRole('button', { name: 'Remove' }))
        await waitFor(() => expect(api.callsTo('DELETE', '/api/entries/e1/')).toHaveLength(1))
    })

    it('a saved run chip offers reopen', async () => {
        const state = makeState({
            entries: [entry('e1', 'Zosia', [run('r1', 23_450, { penalties: ['7'] })])],
        })
        const api = apiWith(state).on('POST', '/api/runs/r1/reopen/', ok(later({})))
        const { user } = await renderApp(api)
        await user.click(screen.getByRole('button', { name: '23.45 · 4 pts' }))
        await user.click(within(await screen.findByRole('dialog')).getByRole('button', { name: 'Reopen to correct' }))
        await waitFor(() => expect(api.callsTo('POST', '/api/runs/r1/reopen/')).toHaveLength(1))
    })
})

describe('ranking view', () => {
    const scored = () =>
        makeState({
            entries: [
                entry('e1', 'Zosia', [run('r1', 20_000, { penalties: ['5'] })]),
                entry('e2', 'Kuba'),
                entry('e3', 'Ola', [run('r2', 31_000, { penalties: ['2', '3A'] }), run('r3', 30_000, { number: 2 })]),
                entry('e4', 'Iga', [run('r4', 24_500)]),
            ],
            selected_entry: 'e2',
        })
    const names = () => competitorRows().map((r) => r.querySelector('.entry-name-text')!.textContent)
    const numbers = () => competitorRows().map((r) => r.querySelector('.entry-pos')!.textContent)

    it('sorts by penalties, then time; shows places; back to the start order', async () => {
        const { user, deps } = await renderApp(apiWith(scored()))
        expect(names()).toEqual(['Zosia', 'Kuba', 'Ola', 'Iga'])
        expect(screen.getByRole('button', { name: 'Start order' })).toHaveAttribute('aria-pressed', 'true')

        await user.click(screen.getByRole('button', { name: 'Ranking' }))
        // clean 24.50 and clean 30.00 beat 20.00 with one penalty; no result goes last
        expect(names()).toEqual(['Iga', 'Ola', 'Zosia', 'Kuba'])
        expect(numbers()).toEqual(['1', '2', '3', '–'])
        expect(screen.getByRole('button', { name: 'Ranking' })).toHaveAttribute('aria-pressed', 'true')
        expect(deps.storageData.get(ORDER_KEY)).toBe('ranking')
        expect(competitorRows()[3]).toHaveClass('entry-selected') // the selection is unchanged

        await user.click(screen.getByRole('button', { name: 'Start order' }))
        expect(names()).toEqual(['Zosia', 'Kuba', 'Ola', 'Iga'])
        expect(numbers()).toEqual(['1', '2', '3', '4'])
    })

    it('marks the run that counts and offers no reordering', async () => {
        const { user } = await renderApp(apiWith(scored()))
        expect(screen.getAllByRole('button', { name: /^Drag to reorder/ })).toHaveLength(4)
        expect(document.querySelector('.chip-best')).toBeNull()

        await user.click(screen.getByRole('button', { name: 'Ranking' }))
        expect(screen.queryByRole('button', { name: /^Drag to reorder/ })).toBeNull()
        expect(screen.queryByRole('button', { name: /^Move .* (up|down)$/ })).toBeNull()
        expect(screen.getByRole('button', { name: 'Edit Ola' })).toBeInTheDocument()
        const best = [...document.querySelectorAll('.chip-best')].map((c) => c.textContent)
        expect(best).toEqual(['24.50 · 0 pts', '30.00 · 0 pts', '20.00 · 4 pts'])
    })

    it('a competitor can still be selected from the ranking', async () => {
        const api = apiWith(scored()).on('POST', `/api/competitions/${CID}/select/`, ok({ ...scored(), server_time: 1_800_000_000_500, selected_entry: 'e4' }))
        const { user } = await renderApp(api)
        await user.click(screen.getByRole('button', { name: 'Ranking' }))
        await user.click(screen.getByRole('button', { name: 'Iga' }))
        expect(api.callsTo('POST', `/api/competitions/${CID}/select/`).map((c) => c.body)).toEqual([{ entry: 'e4' }])
        await waitFor(() => expect(competitorRows()[0]).toHaveClass('entry-selected'))
    })
})

describe('horse names', () => {
    const withHorses = (patch: Partial<ArenaState> = {}) =>
        makeState({
            entries: [
                entry('e1', 'Zosia', [run('r1', 23_450)], 'Stormy'),
                entry('e2', 'Kuba'),
                entry('e3', 'Ola', [], 'Black Thunder'),
            ],
            ...patch,
        })
    const horses = () => competitorRows().map((r) => r.querySelector('.entry-horse')?.textContent ?? null)
    const patches = (api: FakeApi, eid: string) => api.callsTo('PATCH', `/api/entries/${eid}/`).map((c) => c.body)

    it('are shown in the list (both orders) and for the current competitor; nothing where there is none', async () => {
        const api = apiWith(withHorses()).on(
            'POST',
            `/api/competitions/${CID}/select/`,
            ok(withHorses({ server_time: 1_800_000_000_500, selected_entry: 'e2' }))
        )
        const { user } = await renderApp(api)
        expect(horses()).toEqual(['🐴 Horse: Stormy', null, '🐴 Horse: Black Thunder'])
        expect(screen.getByTestId('current-name')).toHaveTextContent('Zosia')
        expect(document.querySelector('.current-horse')).toHaveTextContent('Stormy')

        await user.click(screen.getByRole('button', { name: 'Ranking' }))
        expect(horses()).toEqual(['🐴 Horse: Stormy', null, '🐴 Horse: Black Thunder'])

        await user.click(screen.getByRole('button', { name: 'Kuba' }))
        await waitFor(() => expect(screen.getByTestId('current-name')).toHaveTextContent('Kuba'))
        expect(document.querySelector('.current-horse')).toBeNull()
    })

    it('signing up with a horse sends both and returns to the name field', async () => {
        const api = apiWith(makeState()).on('POST', `/api/competitions/${CID}/entries/`, (call) => {
            const body = call.body as { name: string; horse?: string }
            return ok(later({ entries: [...makeState().entries, entry('e9', body.name, [], body.horse ?? '')] }))
        })
        const { user } = await renderApp(api)
        const name = screen.getByRole('textbox', { name: 'New competitor name' })
        const horse = screen.getByRole('textbox', { name: 'Horse name (optional)' })
        await user.type(name, 'Antek')
        await user.tab()
        expect(horse).toHaveFocus()
        await user.type(horse, '  Storm Chaser {Enter}')
        await waitFor(() => expect(competitorRows()).toHaveLength(4))
        expect(api.callsTo('POST', `/api/competitions/${CID}/entries/`).map((c) => c.body)).toEqual([
            { name: 'Antek', horse: 'Storm Chaser' },
        ])
        expect(horses()[3]).toBe('🐴 Horse: Storm Chaser')
        expect(name).toHaveValue('')
        expect(horse).toHaveValue('')
        expect(name).toHaveFocus()
    })

    it('a horse without a name adds nobody', async () => {
        const api = apiWith(makeState())
        const { user } = await renderApp(api)
        await user.type(screen.getByRole('textbox', { name: 'Horse name (optional)' }), 'Stormy{Enter}')
        expect(screen.getByRole('button', { name: 'Add' })).toBeDisabled()
        expect(api.callsTo('POST', `/api/competitions/${CID}/entries/`)).toEqual([])
    })

    it('editing sends only what changed; moving between the two fields does not save', async () => {
        const api = apiWith(withHorses())
            .on('PATCH', '/api/entries/e2/', ok(withHorses({ server_time: 1_800_000_000_500 })))
            .on('PATCH', '/api/entries/e1/', ok(withHorses({ server_time: 1_800_000_000_600 })))
        const { user } = await renderApp(api)

        // give Kuba a horse: name untouched
        await user.click(screen.getByRole('button', { name: 'Edit Kuba' }))
        const name = screen.getByRole('textbox', { name: 'Competitor name' })
        expect(name).toHaveFocus()
        expect(name).toHaveValue('Kuba')
        await user.tab()
        const horse = screen.getByRole('textbox', { name: 'Horse name' })
        expect(horse).toHaveFocus()
        expect(patches(api, 'e2')).toEqual([]) // the tab did not save
        await user.type(horse, 'Rocket{Enter}')
        await waitFor(() => expect(patches(api, 'e2')).toEqual([{ horse: 'Rocket' }]))
        await waitFor(() => expect(screen.queryByRole('textbox', { name: 'Horse name' })).toBeNull())

        // both at once, saved by clicking elsewhere
        await user.click(screen.getByRole('button', { name: 'Edit Zosia' }))
        await user.clear(screen.getByRole('textbox', { name: 'Competitor name' }))
        await user.type(screen.getByRole('textbox', { name: 'Competitor name' }), 'Zofia')
        await user.clear(screen.getByRole('textbox', { name: 'Horse name' }))
        await user.click(screen.getByText(/^Competitors/))
        await waitFor(() => expect(patches(api, 'e1')).toEqual([{ name: 'Zofia', horse: '' }]))
    })

    it('editing: Escape, no change, or an emptied name send nothing', async () => {
        const api = apiWith(withHorses())
        const { user } = await renderApp(api)

        await user.click(screen.getByRole('button', { name: 'Edit Zosia' }))
        await user.type(screen.getByRole('textbox', { name: 'Horse name' }), ' II{Escape}')
        expect(screen.queryByRole('textbox', { name: 'Horse name' })).toBeNull()

        await user.click(screen.getByRole('button', { name: 'Edit Zosia' }))
        await user.keyboard('{Enter}')
        expect(screen.queryByRole('textbox', { name: 'Horse name' })).toBeNull()

        await user.click(screen.getByRole('button', { name: 'Edit Zosia' }))
        await user.clear(screen.getByRole('textbox', { name: 'Competitor name' }))
        await user.keyboard('{Enter}')
        expect(screen.queryByRole('textbox', { name: 'Competitor name' })).toBeNull()

        expect(patches(api, 'e1')).toEqual([])
        expect(horses()[0]).toBe('🐴 Horse: Stormy')
    })
})

describe('deselecting', () => {
    const select = `/api/competitions/${CID}/select/`

    it('clicking the selected competitor again deselects when no session is open', async () => {
        const api = apiWith(makeState()).on('POST', select, ok(later({ selected_entry: null })))
        const { user } = await renderApp(api)
        await user.click(screen.getByRole('button', { name: /^Zosia/ }))
        expect(api.callsTo('POST', select).map((c) => c.body)).toEqual([{ entry: null }])
        await waitFor(() => expect(screen.getByTestId('current-name')).toHaveTextContent('Nobody selected'))
        expect(screen.getByRole('button', { name: /Start registering/ })).toBeDisabled()
        expect(competitorRows().some((r) => r.classList.contains('entry-selected'))).toBe(false)
    })

    it('not while a session is open for them', async () => {
        const api = apiWith(makeState({ session: session({ entry_id: 'e1' }) }))
        const { user } = await renderApp(api)
        await user.click(screen.getByRole('button', { name: /^Zosia/ }))
        expect(api.callsTo('POST', select)).toEqual([])
    })

    it('a double click selects, and does not deselect again', async () => {
        const api = apiWith(makeState()).on('POST', select, ok(later({ selected_entry: 'e2' })))
        const { user } = await renderApp(api)
        await user.dblClick(screen.getByRole('button', { name: 'Kuba' }))
        await waitFor(() => expect(screen.getByTestId('current-name')).toHaveTextContent('Kuba'))
        expect(api.callsTo('POST', select).map((c) => c.body)).toEqual([{ entry: 'e2' }])
    })
})

describe('scoring rules', () => {
    it('a new competition is created with the time allowed and the usual penalty values, which can be changed', async () => {
        const made = makeState({ competition: { id: 'c9', name: 'Cup', fence_count: 10, ...RULES, time_allowed: 45, overtime_points: 0.25 } })
        const api = apiWith(makeState())
            .on('POST', '/api/competitions/', { status: 201, body: made })
            .on('GET', '/api/competitions/c9/state/', { body: made })
        const { user } = await renderApp(api)
        await user.click(screen.getByRole('button', { name: '+ New competition' }))
        expect(screen.getByRole('textbox', { name: 'Knockdown' })).toHaveValue('4')
        expect(screen.getByRole('textbox', { name: 'Refusal' })).toHaveValue('4')
        expect(screen.getByRole('textbox', { name: 'Second over time' })).toHaveValue('0.4')
        expect(screen.getByRole('textbox', { name: 'Time allowed (s)' })).toHaveValue('')

        await user.type(screen.getByRole('textbox', { name: 'Name' }), 'Cup')
        await user.clear(screen.getByRole('spinbutton', { name: 'Fences' }))
        await user.type(screen.getByRole('spinbutton', { name: 'Fences' }), '10')
        await user.type(screen.getByRole('textbox', { name: 'Time allowed (s)' }), '45')
        await user.clear(screen.getByRole('textbox', { name: 'Second over time' }))
        await user.type(screen.getByRole('textbox', { name: 'Second over time' }), '0,25')
        await user.click(screen.getByRole('button', { name: 'Create' }))
        await waitFor(() => expect(api.callsTo('POST', '/api/competitions/')).toHaveLength(1))
        expect(api.callsTo('POST', '/api/competitions/')[0]!.body).toEqual({
            name: 'Cup',
            fence_count: 10,
            time_allowed: 45,
            knockdown_points: 4,
            refusal_points: 4,
            overtime_points: 0.25,
        })
    })

    it('a value that is not valid blocks the form', async () => {
        const { user } = await renderApp(apiWith(makeState()))
        await user.click(screen.getByRole('button', { name: '+ New competition' }))
        await user.type(screen.getByRole('textbox', { name: 'Name' }), 'Cup')
        const create = screen.getByRole('button', { name: 'Create' })
        expect(create).toBeEnabled()
        for (const [field, bad] of [
            ['Time allowed (s)', '45.5'],
            ['Knockdown', '-1'],
            ['Refusal', 'x'],
            ['Second over time', '0.123'],
        ] as const) {
            const input = screen.getByRole('textbox', { name: field })
            const before = (input as HTMLInputElement).value
            await user.clear(input)
            await user.type(input, bad)
            expect(create, `${field} = ${bad}`).toBeDisabled()
            expect(input).toHaveAttribute('aria-invalid', 'true')
            await user.clear(input)
            if (before) await user.type(input, before)
            expect(create).toBeEnabled()
        }
    })

    it('editing shows the current rules and sends the changed ones', async () => {
        const rules = { ...RULES, time_allowed: 60, knockdown_points: 3 }
        const state = makeState({ competition: { id: CID, name: 'Saturday open', fence_count: 12, ...rules } })
        const api = apiWith(state).on('PATCH', `/api/competitions/${CID}/`, ok({ ...state, server_time: state.server_time + 1 }))
        const { user } = await renderApp(api)
        expect(screen.getByText(/12 fences · time allowed 60 s/)).toBeInTheDocument()
        await user.click(screen.getByRole('button', { name: 'Edit' }))
        expect(screen.getByRole('textbox', { name: 'Time allowed (s)' })).toHaveValue('60')
        expect(screen.getByRole('textbox', { name: 'Knockdown' })).toHaveValue('3')
        await user.clear(screen.getByRole('textbox', { name: 'Time allowed (s)' }))
        await user.click(screen.getByRole('button', { name: 'Save' }))
        await waitFor(() => expect(api.callsTo('PATCH', `/api/competitions/${CID}/`)).toHaveLength(1))
        expect(api.callsTo('PATCH', `/api/competitions/${CID}/`)[0]!.body).toEqual({
            name: 'Saturday open',
            fence_count: 12,
            ...rules,
            time_allowed: null,
        })
    })

    it('the fence panel shows the live score and what the faults cost', async () => {
        const state = makeState({
            competition: { id: CID, name: 'x', fence_count: 12, ...RULES, time_allowed: 20 },
            session: session({ penalties: ['3', '7B'], time_ms: 24_500, fence_points: 8, time_points: 1.6, points: 9.6 }),
        })
        await renderApp(apiWith(state))
        expect(screen.getByTestId('points-now')).toHaveTextContent('8 at fences + 1.6 time = 9.6 pts')
        expect(screen.getByTestId('save-summary')).toHaveTextContent('24.50 · 9.6 pts (8+1.6)')
        expect(document.querySelector('.fences-legend')).toHaveTextContent(
            'knocked down (4 pts) · R: refusal (4 pts) · time allowed 20 s, then 0.4 pts per full second'
        )
    })
})

describe('refusals', () => {
    it('R under a fence adds a refusal with one click; the fence shows how many', async () => {
        let count = 0
        const api = apiWith(makeState({ session: session() })).on('POST', '/api/runs/r-open/refusal/', () => {
            count = (count + 1) % 3
            const refusals: Record<string, number> = count ? { '5': count } : {}
            return ok(makeState({ server_time: 1_800_000_000_100 + count, session: session({ refusals, fence_points: 4 * count, points: 4 * count }) }))
        })
        const { user } = await renderApp(api)
        const r5 = () => screen.getByRole('button', { name: /^Refusal at fence 5/ })
        expect(r5()).toHaveAttribute('aria-pressed', 'false')
        expect(r5()).toHaveTextContent(/^R$/)

        await user.click(r5())
        await waitFor(() => expect(r5()).toHaveAttribute('aria-pressed', 'true'))
        expect(r5()).toHaveAccessibleName('Refusal at fence 5 (1)')
        expect(r5().closest('.fence')).toHaveClass('fence-hit')
        expect(screen.getByRole('button', { name: 'Fence 5' })).toHaveAttribute('aria-pressed', 'false') // not a knockdown

        await user.click(r5())
        await waitFor(() => expect(r5()).toHaveTextContent('R2'))
        expect(api.callsTo('POST', '/api/runs/r-open/refusal/').map((c) => c.body)).toEqual([{ fence: '5' }, { fence: '5' }])
    })

    it('there is one R per fence and it is off without a session', async () => {
        await renderApp(apiWith(makeState()))
        const all = screen.getAllByRole('button', { name: /^Refusal at fence/ })
        expect(all).toHaveLength(12)
        all.forEach((b) => expect(b).toBeDisabled())
    })
})

describe('retire and eliminate', () => {
    const open = () => apiWith(makeState({ session: session({ penalties: ['2'], fence_points: 4, points: 4 }) }))
    const saves = (api: FakeApi) => api.callsTo('POST', '/api/runs/r-open/save/').map((c) => c.body)

    it('retire asks where it ended and saves the round as retired there', async () => {
        const api = open().on('POST', '/api/runs/r-open/save/', ok(later({})))
        const { user } = await renderApp(api)
        await user.click(screen.getByRole('button', { name: 'Retire…' }))
        const dialog = await screen.findByRole('dialog')
        expect(dialog).toHaveTextContent('Retire Zosia?')
        expect(dialog).toHaveTextContent('try 1, 4 pts so far')
        expect(within(dialog).getAllByRole('button', { pressed: false })).toHaveLength(12) // the fences
        await user.click(within(dialog).getByRole('button', { name: '7' }))
        await user.click(within(dialog).getByRole('button', { name: 'Retire at fence 7' }))
        await waitFor(() => expect(saves(api)).toEqual([{ outcome: 'RETIRED', fence: '7' }]))
        expect(screen.queryByRole('dialog')).toBeNull()
    })

    it('eliminate without a fence; picking a fence twice takes it back', async () => {
        const api = open().on('POST', '/api/runs/r-open/save/', ok(later({})))
        const { user } = await renderApp(api)
        await user.click(screen.getByRole('button', { name: 'Eliminate…' }))
        const dialog = await screen.findByRole('dialog')
        expect(dialog).toHaveTextContent('Eliminate Zosia?')
        await user.click(within(dialog).getByRole('button', { name: '3' }))
        await user.click(within(dialog).getByRole('button', { name: '3' }))
        await user.click(within(dialog).getByRole('button', { name: 'Eliminate' }))
        await waitFor(() => expect(saves(api)).toEqual([{ outcome: 'ELIMINATED' }]))
    })

    it('cancel and Escape change nothing; both need an open session', async () => {
        const api = open()
        const { user } = await renderApp(api)
        await user.click(screen.getByRole('button', { name: 'Retire…' }))
        await user.click(within(await screen.findByRole('dialog')).getByRole('button', { name: 'Cancel' }))
        expect(screen.queryByRole('dialog')).toBeNull()
        await user.click(screen.getByRole('button', { name: 'Eliminate…' }))
        await screen.findByRole('dialog')
        await user.keyboard('{Escape}')
        expect(screen.queryByRole('dialog')).toBeNull()
        expect(saves(api)).toEqual([])
    })

    it('are off without a session', async () => {
        await renderApp(apiWith(makeState()))
        expect(screen.getByRole('button', { name: 'Retire…' })).toBeDisabled()
        expect(screen.getByRole('button', { name: 'Eliminate…' })).toBeDisabled()
    })

    it('a round that was not completed shows as RET / ELI in the list and in the ranking', async () => {
        const state = makeState({
            entries: [
                entry('e1', 'Zosia', [run('r1', null, { outcome: 'RETIRED', outcome_fence: '7', penalties: ['2'] })]),
                entry('e2', 'Kuba', [run('r2', 30_000)]),
                entry('e3', 'Ola', [run('r3', null, { outcome: 'ELIMINATED' })]),
                entry('e4', 'Iga'),
            ],
        })
        const { user } = await renderApp(apiWith(state))
        expect(screen.getByRole('button', { name: 'RET @7 · 4 pts' })).toHaveClass('chip-unfinished')
        expect(screen.getByRole('button', { name: 'ELI · 0 pts' })).toHaveClass('chip-unfinished')
        await user.click(screen.getByRole('button', { name: 'Ranking' }))
        expect(competitorRows().map((r) => `${r.querySelector('.entry-pos')!.textContent} ${r.querySelector('.entry-name-text')!.textContent}`)).toEqual([
            '1 Kuba',
            'RET Zosia',
            'ELI Ola',
            '– Iga',
        ])
        // the chip tells the whole story
        await user.click(screen.getByRole('button', { name: 'RET @7 · 4 pts' }))
        expect(await screen.findByRole('dialog')).toHaveTextContent('Saved: retired at fence 7, 4 pts — knocked down 2.')
    })
})

describe('start bell', () => {
    it('rings when the operator starts registering', async () => {
        const api = apiWith(makeState()).on('POST', `/api/competitions/${CID}/capture/`, ok(later({ session: session() })))
        const { user, deps } = await renderApp(api)
        await user.click(screen.getByRole('button', { name: /Start registering/ }))
        await waitFor(() => expect(screen.getByTestId('status-badge')).toHaveTextContent('REGISTERING'))
        expect(deps.bell.rings).toBe(1)
    })

    it('can be switched off and on in the top bar; switching on rings it', async () => {
        const api = apiWith(makeState()).on('POST', `/api/competitions/${CID}/capture/`, ok(later({ session: session() })))
        const { user, deps } = await renderApp(api)
        await user.click(screen.getByRole('button', { name: /Bell on/ }))
        expect(screen.getByRole('button', { name: /Bell off/ })).toHaveAttribute('aria-pressed', 'false')
        await user.click(screen.getByRole('button', { name: /Start registering/ }))
        await waitFor(() => expect(screen.getByTestId('status-badge')).toHaveTextContent('REGISTERING'))
        expect(deps.bell.rings).toBe(0)

        await user.click(screen.getByRole('button', { name: /Bell off/ }))
        expect(screen.getByRole('button', { name: /Bell on/ })).toHaveAttribute('aria-pressed', 'true')
        expect(deps.bell.rings).toBe(1)
    })
})

describe('session controls', () => {
    it('start / stop registering calls capture', async () => {
        const api = apiWith(makeState()).on('POST', `/api/competitions/${CID}/capture/`, (call) =>
            ok(
                later({
                    server_time: (call.body as { on: boolean }).on ? 1_800_000_000_100 : 1_800_000_000_200,
                    session: session({ capturing: (call.body as { on: boolean }).on }),
                })
            )
        )
        const { user } = await renderApp(api)
        expect(screen.getByTestId('status-badge')).toHaveTextContent('IDLE')
        await user.click(screen.getByRole('button', { name: /Start registering/ }))
        await waitFor(() => expect(screen.getByTestId('status-badge')).toHaveTextContent('REGISTERING'))
        await user.click(screen.getByRole('button', { name: /Stop registering/ }))
        await waitFor(() => expect(screen.getByTestId('status-badge')).toHaveTextContent('PAUSED'))
        expect(api.callsTo('POST', `/api/competitions/${CID}/capture/`).map((c) => c.body)).toEqual([{ on: true }, { on: false }])
    })

    it('capture is disabled without a selection', async () => {
        await renderApp(apiWith(makeState({ selected_entry: null })))
        expect(screen.getByRole('button', { name: /Start registering/ })).toBeDisabled()
    })

    it('fence "7" and "B" under "9" toggle penalties "7" / "9B"; penalised ones are highlighted', async () => {
        let pens: string[] = []
        let t = 1_800_000_000_000
        const api = apiWith(makeState({ session: session() })).on('POST', '/api/runs/r-open/penalty/', (call) => {
            const f = (call.body as { fence: string }).fence
            pens = pens.includes(f) ? pens.filter((p) => p !== f) : [...pens, f]
            return ok(makeState({ server_time: ++t, session: session({ penalties: pens }) }))
        })
        const { user } = await renderApp(api)
        await user.click(screen.getByRole('button', { name: 'Fence 7' }))
        await user.click(screen.getByRole('button', { name: 'Fence 9B' }))
        expect(api.callsTo('POST', '/api/runs/r-open/penalty/').map((c) => c.body)).toEqual([{ fence: '7' }, { fence: '9B' }])
        await waitFor(() => expect(screen.getByRole('button', { name: 'Fence 9B' })).toHaveClass('penalised'))
        expect(screen.getByRole('button', { name: 'Fence 7' })).toHaveClass('penalised')
        expect(screen.getByRole('button', { name: 'Fence 7' })).toHaveAttribute('aria-pressed', 'true')
        expect(screen.getByRole('button', { name: 'Fence 9' })).not.toHaveClass('penalised')
        expect(screen.getByRole('button', { name: 'Fence 9A' })).not.toHaveClass('penalised')
    })

    it('fence grid has fence_count blocks in two rows and is disabled without a session', async () => {
        await renderApp(apiWith(makeState({ competition: { id: CID, name: 'x', fence_count: 13, ...RULES } })))
        const rows = document.querySelectorAll('.fence-row')
        expect([...rows].map((r) => r.querySelectorAll('.fence').length)).toEqual([7, 6])
        expect(screen.getByRole('button', { name: 'Fence 13' })).toBeDisabled()
    })
})

describe('signal columns', () => {
    const t0 = new Date(2026, 10, 14, 10, 0, 0, 0).getTime()
    const sess = (patch = {}) =>
        session({
            starts: [
                { id: 's1', sensor_time: t0, in_session: true },
                { id: 's2', sensor_time: t0 + 5_000, in_session: false },
            ],
            finishes: [
                { id: 'f1', sensor_time: t0 + 23_450, in_session: true, time_ms: 23_450 },
                { id: 'f2', sensor_time: t0 + 30_000, in_session: true, time_ms: 30_000 },
            ],
            start_id: 's1',
            finish_id: 'f2',
            time_ms: 30_000,
            ...patch,
        })

    it('marks the counting signals and picks another one', async () => {
        const api = apiWith(makeState({ session: sess() })).on('POST', '/api/runs/r-open/pick/', ok(later({ session: sess({ finish_id: 'f1', finish_auto: false, time_ms: 23_450 }) })))
        const { user } = await renderApp(api)
        const starts = screen.getByRole('group', { name: 'START signals' })
        const finishes = screen.getByRole('group', { name: 'FINISH signals' })
        const counting = within(starts).getByRole('button', { pressed: true })
        expect(counting).toHaveTextContent('10:00:00.00')
        expect(counting).toHaveTextContent('auto')
        expect(within(starts).getByRole('button', { pressed: false })).toHaveTextContent('outside session')
        expect(within(finishes).getByRole('button', { pressed: true })).toHaveTextContent('30.00')

        await user.click(within(finishes).getByRole('button', { name: /23\.45/ }))
        expect(api.callsTo('POST', '/api/runs/r-open/pick/').map((c) => c.body)).toEqual([{ finish: 'f1' }])
        await waitFor(() => expect(within(finishes).getByRole('button', { pressed: true })).toHaveTextContent('picked'))
        expect(screen.getByTestId('readout')).toHaveTextContent('23.45')
    })

    it('clicking the picked one returns to automatic (null); clicking the auto one does nothing', async () => {
        const api = apiWith(makeState({ session: sess({ start_id: 's2', start_auto: false }) })).on(
            'POST',
            '/api/runs/r-open/pick/',
            ok(later({ session: sess() }))
        )
        const { user } = await renderApp(api)
        const starts = screen.getByRole('group', { name: 'START signals' })
        await user.click(within(starts).getByRole('button', { pressed: true }))
        expect(api.callsTo('POST', '/api/runs/r-open/pick/').map((c) => c.body)).toEqual([{ start: null }])
        await waitFor(() => expect(within(starts).getByRole('button', { pressed: true })).toHaveTextContent('auto'))
        await user.click(within(starts).getByRole('button', { pressed: true }))
        expect(api.callsTo('POST', '/api/runs/r-open/pick/')).toHaveLength(1)
    })

    it('empty columns say what is awaited', async () => {
        await renderApp(apiWith(makeState({ session: session() })))
        expect(screen.getByText('Waiting for the start gate…')).toBeInTheDocument()
        expect(screen.getByText(/No FINISH yet/)).toBeInTheDocument()
        expect(screen.getByTestId('readout')).toHaveTextContent('0.00')
    })
})

describe('save', () => {
    it('saves straight away when there is a time', async () => {
        const api = apiWith(makeState({ session: session({ time_ms: 23_450, penalties: ['9B', '7'], fence_points: 8, points: 8 }) })).on(
            'POST',
            '/api/runs/r-open/save/',
            ok(later({ selected_entry: 'e2' }))
        )
        const { user } = await renderApp(api)
        expect(screen.getByTestId('save-summary')).toHaveTextContent('23.45 · 8 pts')
        await user.click(screen.getByRole('button', { name: 'Save & next' }))
        expect(screen.queryByRole('dialog')).not.toBeInTheDocument()
        await waitFor(() => expect(api.callsTo('POST', '/api/runs/r-open/save/')).toHaveLength(1))
        await waitFor(() => expect(screen.getByTestId('current-name')).toHaveTextContent('Kuba'))
    })

    it('asks first when there is no time', async () => {
        const api = apiWith(makeState({ session: session() })).on('POST', '/api/runs/r-open/save/', ok(later({})))
        const { user } = await renderApp(api)
        expect(screen.getByTestId('save-summary')).toHaveTextContent('no time')
        await user.click(screen.getByRole('button', { name: 'Save & next' }))
        const dialog = await screen.findByRole('dialog')
        expect(dialog).toHaveTextContent('Save without a time?')
        expect(api.callsTo('POST', '/api/runs/r-open/save/')).toHaveLength(0)
        await user.click(within(dialog).getByRole('button', { name: 'Save without time' }))
        await waitFor(() => expect(api.callsTo('POST', '/api/runs/r-open/save/')).toHaveLength(1))
    })
})

describe('banners & errors', () => {
    it('shows the busy banner and switches competition', async () => {
        const other = makeState({ competition: { id: 'c2', name: 'Morning cup', fence_count: 12, ...RULES } })
        const api = apiWith(makeState({ busy: { competition_id: 'c2', competition_name: 'Morning cup' } })).on(
            'GET',
            '/api/competitions/c2/state/',
            ok(other)
        )
        const { user, deps } = await renderApp(api)
        expect(screen.getByText(/A session is open in “Morning cup”/)).toBeInTheDocument()
        expect(screen.getByRole('button', { name: /Start registering/ })).toBeDisabled()
        await user.click(screen.getByRole('button', { name: 'Switch to “Morning cup”' }))
        await waitFor(() => expect(api.callsTo('GET', '/api/competitions/c2/state/')).toHaveLength(1))
        expect(deps.sockets.at(-1)!.url).toMatch(/\/ws\/live\/c2\/$/)
    })

    it('shows server errors in a dismissible bar', async () => {
        const api = apiWith(makeState()).on('POST', `/api/competitions/${CID}/capture/`, {
            status: 409,
            body: { error: 'A session is open in another competition.' },
        })
        const { user } = await renderApp(api)
        await user.click(screen.getByRole('button', { name: /Start registering/ }))
        const alert = await screen.findByText('A session is open in another competition.')
        await user.click(within(alert.closest('.error') as HTMLElement).getByRole('button', { name: 'Dismiss error' }))
        expect(screen.queryByText('A session is open in another competition.')).not.toBeInTheDocument()
    })

    it('shows when the connection is lost', async () => {
        const { deps } = await renderApp(apiWith(makeState()))
        deps.sockets[0]!.open()
        await screen.findByText('Live')
        deps.sockets[0]!.drop()
        expect(await screen.findByText(/Connection to the server lost/)).toBeInTheDocument()
    })

    it('device lights reflect state.devices', async () => {
        await renderApp(apiWith(makeState()))
        expect(screen.getByTestId('device-START')).toHaveClass('device-ok')
        expect(screen.getByTestId('device-FINISH')).toHaveClass('device-warn')
        expect(screen.getByTestId('device-TIMER')).toHaveClass('device-off')
    })

    it('first run: no competitions → create form', async () => {
        const created = makeState({ competition: { id: 'new', name: 'Spring', fence_count: 18, ...RULES } })
        const api = new FakeApi()
            .on('GET', '/api/competitions/', { body: [] })
            .on('POST', '/api/competitions/', { status: 201, body: created })
            .on('GET', '/api/competitions/new/state/', { body: created })
        const deps = makeDeps(api)
        const user = userEvent.setup()
        render(<App store={new CompetitionStore(deps)} />)
        await screen.findByText('Create the first competition')
        await user.type(screen.getByRole('textbox', { name: 'Name' }), 'Spring')
        const fences = screen.getByRole('spinbutton', { name: 'Fences' })
        await user.clear(fences)
        await user.type(fences, '18')
        await user.click(screen.getByRole('button', { name: 'Create competition' }))
        await screen.findByTestId('current-name')
        expect(api.callsTo('POST', '/api/competitions/')[0]?.body).toEqual({ name: 'Spring', fence_count: 18, ...RULES })
    })
})
