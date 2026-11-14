import { describe, expect, it } from 'vitest'
import { CID, FakeApi, apiWith, makeDeps, makeState, session, summary } from '../test/fakes'
import { BELL_KEY, CompetitionStore, ORDER_KEY, STORAGE_KEY, reconnectDelay, startedRegistering } from './store'

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

const T0 = 1_800_000_000_000
const idle = (dt: number) => makeState({ server_time: T0 + dt })
const registering = (dt: number, run_id = 'r-open') => makeState({ server_time: T0 + dt, session: session({ run_id }) })
const paused = (dt: number) => makeState({ server_time: T0 + dt, session: session({ capturing: false }) })

describe('startedRegistering', () => {
    it('is true when a session starts or resumes', () => {
        expect(startedRegistering(idle(0), registering(1))).toBe(true)
        expect(startedRegistering(paused(0), registering(1))).toBe(true)
    })

    it('is true when another run is being registered than before', () => {
        expect(startedRegistering(registering(0, 'run-a'), registering(1, 'run-b'))).toBe(true)
    })

    it('is false when nothing started', () => {
        expect(startedRegistering(null, registering(1))).toBe(false)
        expect(startedRegistering(registering(0), registering(1))).toBe(false)
        expect(startedRegistering(registering(0), paused(1))).toBe(false)
        expect(startedRegistering(registering(0), idle(1))).toBe(false)
        expect(startedRegistering(idle(0), paused(1))).toBe(false)
        expect(startedRegistering(idle(0), idle(1))).toBe(false)
    })
})

describe('start bell', () => {
    async function online(first = idle(0)) {
        const api = apiWith(first)
        const { store, deps } = await started(api)
        const ws = deps.sockets[0]!
        ws.open()
        ws.send({ type: 'state', state: first }) // the server's greeting
        return { api, store, deps, ws }
    }

    it('rings once when a push reports that registering started', async () => {
        const { deps, ws } = await online()
        ws.send({ type: 'state', state: registering(10) })
        expect(deps.bell.rings).toBe(1)
        ws.send({ type: 'state', state: registering(20) }) // a penalty, a signal ...
        expect(deps.bell.rings).toBe(1)
    })

    it('rings again when registering is resumed after a stop', async () => {
        const { deps, ws } = await online()
        ws.send({ type: 'state', state: registering(10) })
        ws.send({ type: 'state', state: paused(20) })
        expect(deps.bell.rings).toBe(1)
        ws.send({ type: 'state', state: registering(30) })
        expect(deps.bell.rings).toBe(2)
    })

    it("rings for the operator's own click, and not a second time for the push that follows", async () => {
        const { api, store, deps, ws } = await online()
        api.on('POST', `/api/competitions/${CID}/capture/`, { body: registering(10) })
        await store.capture(true)
        expect(deps.bell.rings).toBe(1)
        ws.send({ type: 'state', state: registering(10) })
        expect(deps.bell.rings).toBe(1)
    })

    it('rings for the own click even while the live connection is down', async () => {
        const { api, store, deps, ws } = await online()
        ws.drop()
        api.on('POST', `/api/competitions/${CID}/capture/`, { body: registering(10) })
        await store.capture(true)
        expect(deps.bell.rings).toBe(1)
    })

    it('stays silent for a session that is already registering when the page loads', async () => {
        const { deps, ws } = await online(registering(0))
        expect(deps.bell.rings).toBe(0)
        ws.send({ type: 'state', state: registering(10) })
        expect(deps.bell.rings).toBe(0)
    })

    it('stays silent for a start it only learns about after reconnecting, then rings for the next one', async () => {
        const { api, store, deps, ws } = await online()
        ws.drop()
        api.on('GET', `/api/competitions/${CID}/state/`, { body: registering(10) })
        deps.runTimers()
        const again = deps.sockets[1]!
        again.open()
        again.send({ type: 'state', state: registering(11) }) // greeting of the new socket
        await flush() // the re-fetch
        expect(store.getSnapshot().state?.session?.capturing).toBe(true)
        expect(deps.bell.rings).toBe(0)

        again.send({ type: 'state', state: idle(20) })
        again.send({ type: 'state', state: registering(30, 'r-next') })
        expect(deps.bell.rings).toBe(1)
    })

    it('greeting first, re-fetch second: still silent', async () => {
        const { api, deps, ws } = await online()
        ws.drop()
        api.on('GET', `/api/competitions/${CID}/state/`, { body: registering(12) })
        deps.runTimers()
        deps.sockets[1]!.open()
        await flush() // the re-fetch answers before the greeting arrives
        deps.sockets[1]!.send({ type: 'state', state: registering(12) })
        expect(deps.bell.rings).toBe(0)
    })

    it('does not ring for a state that is ignored as older or of another competition', async () => {
        const { deps, ws } = await online(idle(100))
        ws.send({ type: 'state', state: registering(50) })
        ws.send({
            type: 'state',
            state: { ...registering(200), competition: { id: 'zz', name: 'x', fence_count: 1 } },
        })
        expect(deps.bell.rings).toBe(0)
    })

    it('switched off: silent and remembered; switching on rings once as a test', async () => {
        const { store, deps, ws } = await online()
        expect(store.getSnapshot().bell).toBe(true)
        store.setBell(false)
        expect(store.getSnapshot().bell).toBe(false)
        expect(deps.storageData.get(BELL_KEY)).toBe('off')
        ws.send({ type: 'state', state: registering(10) })
        expect(deps.bell.rings).toBe(0)

        store.setBell(true)
        expect(store.getSnapshot().bell).toBe(true)
        expect(deps.storageData.get(BELL_KEY)).toBe('on')
        expect(deps.bell.rings).toBe(1)
    })
})

describe('settings of this browser', () => {
    it('default to start order with the bell on', () => {
        const snap = new CompetitionStore(makeDeps(new FakeApi())).getSnapshot()
        expect(snap.listOrder).toBe('start')
        expect(snap.bell).toBe(true)
    })

    it('are read from storage', () => {
        const deps = makeDeps(new FakeApi())
        deps.storageData.set(ORDER_KEY, 'ranking')
        deps.storageData.set(BELL_KEY, 'off')
        const snap = new CompetitionStore(deps).getSnapshot()
        expect(snap.listOrder).toBe('ranking')
        expect(snap.bell).toBe(false)
    })

    it('fall back to the defaults on anything unexpected, or without storage', () => {
        const deps = makeDeps(new FakeApi())
        deps.storageData.set(ORDER_KEY, 'sideways')
        deps.storageData.set(BELL_KEY, 'maybe')
        expect(new CompetitionStore(deps).getSnapshot()).toMatchObject({ listOrder: 'start', bell: true })
        const store = new CompetitionStore({ ...makeDeps(new FakeApi()), storage: null })
        expect(store.getSnapshot()).toMatchObject({ listOrder: 'start', bell: true })
        store.setListOrder('ranking') // no storage: still works for this page
        expect(store.getSnapshot().listOrder).toBe('ranking')
    })

    it('the list order is remembered', () => {
        const deps = makeDeps(new FakeApi())
        const store = new CompetitionStore(deps)
        store.setListOrder('ranking')
        expect(store.getSnapshot().listOrder).toBe('ranking')
        expect(deps.storageData.get(ORDER_KEY)).toBe('ranking')
        store.setListOrder('start')
        expect(deps.storageData.get(ORDER_KEY)).toBe('start')
    })
})
