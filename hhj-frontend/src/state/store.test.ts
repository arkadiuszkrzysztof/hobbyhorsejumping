import { describe, expect, it } from 'vitest'
import { CID, FakeApi, apiWith, makeDeps, makeState, summary } from '../test/fakes'
import { CompetitionStore, STORAGE_KEY, reconnectDelay } from './store'

const flush = () => new Promise((r) => setTimeout(r, 0))

async function started(api: FakeApi) {
    const deps = makeDeps(api)
    const store = new CompetitionStore(deps)
    await store.start()
    await flush()
    return { store, deps }
}

describe('CompetitionStore', () => {
    it('opens the newest competition, fetches its state and follows it over a WebSocket', async () => {
        const api = apiWith(makeState())
        const { store, deps } = await started(api)
        expect(store.getSnapshot().state?.competition.name).toBe('Saturday open')
        expect(deps.sockets).toHaveLength(1)
        expect(deps.sockets[0]!.url).toBe(`ws://arena.local:3000/ws/live/${CID}/`)
        expect(deps.storageData.get(STORAGE_KEY)).toBe(CID)
        expect(store.getSnapshot().connection).toBe('connecting')
        deps.sockets[0]!.open()
        expect(store.getSnapshot().connection).toBe('online')
    })

    it('reopens the remembered competition, falling back to the newest', async () => {
        const other = makeState({ competition: { id: 'c2', name: 'Sunday', fence_count: 18 } })
        const api = apiWith(makeState(), [summary(), summary('c2', 'Sunday')]).on(
            'GET',
            '/api/competitions/c2/state/',
            { body: other }
        )
        const deps = makeDeps(api)
        deps.storageData.set(STORAGE_KEY, 'c2')
        const store = new CompetitionStore(deps)
        await store.start()
        await flush()
        expect(store.getSnapshot().state?.competition.name).toBe('Sunday')
    })

    it('enters first-run when there are no competitions', async () => {
        const api = new FakeApi().on('GET', '/api/competitions/', { body: [] })
        const { store } = await started(api)
        expect(store.getSnapshot().phase).toBe('first-run')
    })

    it('applies only the newest state (server_time)', async () => {
        const api = apiWith(makeState({ server_time: 1000 }))
        const { store, deps } = await started(api)
        const ws = deps.sockets[0]!
        ws.send({ type: 'state', state: makeState({ server_time: 3000, selected_entry: 'e3' }) })
        expect(store.getSnapshot().state?.selected_entry).toBe('e3')
        ws.send({ type: 'state', state: makeState({ server_time: 2000, selected_entry: 'e2' }) })
        expect(store.getSnapshot().state?.selected_entry).toBe('e3')
        // ...and an HTTP answer older than the push is ignored too
        api.on('POST', `/api/competitions/${CID}/select/`, { body: makeState({ server_time: 2500, selected_entry: 'e2' }) })
        await store.select('e2')
        expect(store.getSnapshot().state?.selected_entry).toBe('e3')
    })

    it('ignores states of another competition', async () => {
        const { store, deps } = await started(apiWith(makeState({ server_time: 1000 })))
        deps.sockets[0]!.send({
            type: 'state',
            state: makeState({ server_time: 5000, competition: { id: 'zz', name: 'x', fence_count: 1 } }),
        })
        expect(store.getSnapshot().state?.competition.id).toBe(CID)
    })

    it('applies device pushes and tracks the server clock offset', async () => {
        const { store, deps } = await started(apiWith(makeState({ server_time: 1_000_000 + 400 })))
        expect(store.getSnapshot().clockOffset).toBe(400)
        deps.clock.now = 2_000_000
        deps.sockets[0]!.send({
            type: 'devices',
            server_time: 2_000_900,
            devices: [{ role: 'TIMER', online: true, ok: null }],
        })
        expect(store.getSnapshot().state?.devices).toEqual([{ role: 'TIMER', online: true, ok: null }])
        expect(store.getSnapshot().clockOffset).toBe(900)
    })

    it('reconnects with backoff after the socket drops and re-fetches the state', async () => {
        const api = apiWith(makeState({ server_time: 1000 }))
        const { store, deps } = await started(api)
        deps.sockets[0]!.open()
        expect(api.callsTo('GET', `/api/competitions/${CID}/state/`)).toHaveLength(1)

        deps.sockets[0]!.drop()
        expect(store.getSnapshot().connection).toBe('offline')
        expect(deps.timers.map((t) => t.ms)).toEqual([reconnectDelay(0)])

        deps.runTimers() // first retry fails
        expect(deps.sockets).toHaveLength(2)
        deps.sockets[1]!.drop()
        expect(deps.timers.map((t) => t.ms)).toEqual([reconnectDelay(1)])
        expect(reconnectDelay(1)).toBeGreaterThan(reconnectDelay(0))

        api.on('GET', `/api/competitions/${CID}/state/`, { body: makeState({ server_time: 9000, selected_entry: 'e2' }) })
        deps.runTimers()
        deps.sockets[2]!.open()
        await flush()
        expect(store.getSnapshot().connection).toBe('online')
        expect(api.callsTo('GET', `/api/competitions/${CID}/state/`)).toHaveLength(2)
        expect(store.getSnapshot().state?.selected_entry).toBe('e2')
    })

    it('backoff is capped', () => {
        expect(reconnectDelay(20)).toBe(10_000)
    })

    it('surfaces the error string of a 400 / 409 and clears the in-flight flag', async () => {
        const api = apiWith(makeState()).on('POST', `/api/competitions/${CID}/capture/`, {
            status: 409,
            body: { error: 'Nobody is selected.' },
        })
        const { store } = await started(api)
        const p = store.capture(true)
        expect(store.getSnapshot().pending.has('capture')).toBe(true)
        const r = await p
        expect(r).toEqual({ ok: false, status: 409, error: 'Nobody is selected.' })
        expect(store.getSnapshot().pending.has('capture')).toBe(false)
        expect(store.getSnapshot().errors.map((e) => e.message)).toEqual(['Nobody is selected.'])

        api.on('POST', `/api/competitions/${CID}/entries/`, { status: 400, body: { error: 'Name is required.' } })
        await store.addEntry(' ')
        expect(store.getSnapshot().errors.map((e) => e.message)).toContain('Name is required.')

        store.dismissError(store.getSnapshot().errors[0]!.id)
        expect(store.getSnapshot().errors.map((e) => e.message)).toEqual(['Name is required.'])
    })

    it('reports a network failure', async () => {
        const api = apiWith(makeState())
        const { store } = await started(api)
        api.on('POST', `/api/competitions/${CID}/capture/`, () => {
            throw new TypeError('Failed to fetch')
        })
        // FakeApi turns handler exceptions into rejected fetches
        const r = await store.capture(true)
        expect(r.ok).toBe(false)
        expect(store.getSnapshot().errors[0]?.message).toMatch(/Cannot reach the server/)
    })

    it('keeps a select 409 quiet (the UI asks about moving the session)', async () => {
        const api = apiWith(makeState()).on('POST', `/api/competitions/${CID}/select/`, {
            status: 409,
            body: { error: 'Session open for Zosia' },
        })
        const { store } = await started(api)
        const r = await store.select('e2')
        expect(r).toMatchObject({ ok: false, status: 409 })
        expect(store.getSnapshot().errors).toEqual([])
    })
})
