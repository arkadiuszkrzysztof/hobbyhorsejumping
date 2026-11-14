import { render, screen, waitFor, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { describe, expect, it } from 'vitest'
import type { ArenaState } from './api/types'
import { App } from './App'
import { CompetitionStore } from './state/store'
import { CID, FakeApi, apiWith, entry, makeDeps, makeState, session } from './test/fakes'

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
                    { id: 'r1', number: 1, status: 'SAVED', time_ms: 23_450, penalties: ['7', '9B'] },
                    { id: 'r2', number: 2, status: 'SAVED', time_ms: null, penalties: [] },
                ]),
                entry('e2', 'Kuba', [{ id: 'r3', number: 1, status: 'OPEN', time_ms: null, penalties: [] }]),
                entry('e3', 'Ola'),
            ],
            session: session({ run_id: 'r3', entry_id: 'e2' }),
            selected_entry: 'e2',
        })
        await renderApp(apiWith(state))
        const rows = competitorRows()
        expect(rows.map((r) => r.querySelector('.entry-name')!.textContent)).toEqual(['Zosia', 'KubaSELECTED', 'Ola'])
        expect(rows.map((r) => r.querySelector('.entry-pos')!.textContent)).toEqual(['1', '2', '3'])
        expect(within(rows[0]!).getByText('23.45 · 2 pen')).toBeInTheDocument()
        expect(within(rows[0]!).getByText('— · 0 pen')).toBeInTheDocument()
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
            entries: [entry('e1', 'Zosia', [{ id: 'r1', number: 1, status: 'SAVED', time_ms: 1, penalties: [] }])],
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
            entries: [entry('e1', 'Zosia', [{ id: 'r1', number: 1, status: 'SAVED', time_ms: 23_450, penalties: ['7'] }])],
        })
        const api = apiWith(state).on('POST', '/api/runs/r1/reopen/', ok(later({})))
        const { user } = await renderApp(api)
        await user.click(screen.getByRole('button', { name: '23.45 · 1 pen' }))
        await user.click(within(await screen.findByRole('dialog')).getByRole('button', { name: 'Reopen to correct' }))
        await waitFor(() => expect(api.callsTo('POST', '/api/runs/r1/reopen/')).toHaveLength(1))
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
        await renderApp(apiWith(makeState({ competition: { id: CID, name: 'x', fence_count: 13 } })))
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
        const api = apiWith(makeState({ session: session({ time_ms: 23_450, penalties: ['9B', '7'] }) })).on(
            'POST',
            '/api/runs/r-open/save/',
            ok(later({ selected_entry: 'e2' }))
        )
        const { user } = await renderApp(api)
        expect(screen.getByTestId('save-summary')).toHaveTextContent('23.45 · 2 penalties (7, 9B)')
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
        const other = makeState({ competition: { id: 'c2', name: 'Morning cup', fence_count: 12 } })
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
        const created = makeState({ competition: { id: 'new', name: 'Spring', fence_count: 18 } })
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
        expect(api.callsTo('POST', '/api/competitions/')[0]?.body).toEqual({ name: 'Spring', fence_count: 18 })
    })
})
