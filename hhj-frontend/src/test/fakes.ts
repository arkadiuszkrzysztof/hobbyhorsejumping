// Test doubles: a scriptable fake API (fetch), a fake WebSocket and state fixtures.
import type { ArenaState, CompetitionSummary, Entry, Session } from '../api/types'
import type { StoreDeps, WebSocketLike } from '../state/store'

export interface Call {
    method: string
    path: string
    body: unknown
}

export interface Reply {
    status?: number
    body?: unknown
}

type Handler = (call: Call) => Reply | Promise<Reply>

/** fetch replacement: route "METHOD /path" → handler; every call is recorded. */
export class FakeApi {
    calls: Call[] = []
    private routes = new Map<string, Handler>()
    fallback: Handler = (c) => ({ status: 404, body: { error: `no fake route for ${c.method} ${c.path}` } })

    on(method: string, path: string, handler: Handler | Reply) {
        this.routes.set(`${method} ${path}`, typeof handler === 'function' ? handler : () => handler)
        return this
    }

    fetch = async (input: string, init?: RequestInit): Promise<Response> => {
        const call: Call = {
            method: init?.method ?? 'GET',
            path: input,
            body: init?.body ? JSON.parse(String(init.body)) : undefined,
        }
        this.calls.push(call)
        const handler = this.routes.get(`${call.method} ${call.path}`) ?? this.fallback
        const reply = await handler(call)
        return new Response(reply.body === undefined ? '' : JSON.stringify(reply.body), {
            status: reply.status ?? 200,
            headers: { 'Content-Type': 'application/json' },
        })
    }

    callsTo(method: string, path: string) {
        return this.calls.filter((c) => c.method === method && c.path === path)
    }
}

export class FakeSocket implements WebSocketLike {
    onopen: WebSocketLike['onopen'] = null
    onmessage: WebSocketLike['onmessage'] = null
    onclose: WebSocketLike['onclose'] = null
    onerror: WebSocketLike['onerror'] = null
    closed = false
    constructor(readonly url: string) {}
    open() {
        this.onopen?.(new Event('open'))
    }
    send(msg: unknown) {
        this.onmessage?.(new MessageEvent('message', { data: JSON.stringify(msg) }))
    }
    drop() {
        this.onclose?.(new CloseEvent('close'))
    }
    close() {
        this.closed = true
    }
}

export interface TestDeps extends StoreDeps {
    sockets: FakeSocket[]
    timers: { fn: () => void; ms: number; cleared: boolean }[]
    runTimers: () => void
    storageData: Map<string, string>
    clock: { now: number }
    /** How often the store rang the start bell. */
    bell: { rings: number }
}

export function makeDeps(api: FakeApi): TestDeps {
    const sockets: FakeSocket[] = []
    const timers: TestDeps['timers'] = []
    const storageData = new Map<string, string>()
    const clock = { now: 1_000_000 }
    const bell = { rings: 0 }
    return {
        fetch: api.fetch,
        createWebSocket: (url) => {
            const s = new FakeSocket(url)
            sockets.push(s)
            return s
        },
        location: { protocol: 'http:', host: 'arena.local:3000' },
        storage: {
            getItem: (k) => storageData.get(k) ?? null,
            setItem: (k, v) => void storageData.set(k, v),
            removeItem: (k) => void storageData.delete(k),
        },
        now: () => clock.now,
        setTimeout: (fn, ms) => {
            const t = { fn, ms, cleared: false }
            timers.push(t)
            return t
        },
        clearTimeout: (h) => {
            ;(h as { cleared: boolean }).cleared = true
        },
        ringBell: () => {
            bell.rings += 1
        },
        bell,
        sockets,
        timers,
        storageData,
        clock,
        runTimers: () => {
            const due = timers.splice(0).filter((t) => !t.cleared)
            due.forEach((t) => t.fn())
        },
    }
}

export const CID = 'c1'

export function entry(id: string, name: string, runs: Entry['runs'] = [], horse = ''): Entry {
    return { id, name, horse, runs }
}

export function session(patch: Partial<Session> = {}): Session {
    return {
        run_id: 'r-open',
        entry_id: 'e1',
        number: 1,
        capturing: true,
        penalties: [],
        starts: [],
        finishes: [],
        start_id: null,
        finish_id: null,
        start_auto: true,
        finish_auto: true,
        time_ms: null,
        ...patch,
    }
}

export function makeState(patch: Partial<ArenaState> = {}): ArenaState {
    return {
        server_time: 1_800_000_000_000,
        competition: { id: CID, name: 'Saturday open', fence_count: 12 },
        entries: [entry('e1', 'Zosia'), entry('e2', 'Kuba'), entry('e3', 'Ola')],
        selected_entry: 'e1',
        session: null,
        busy: null,
        devices: [
            { role: 'START', online: true, ok: true },
            { role: 'FINISH', online: true, ok: false },
            { role: 'TIMER', online: false, ok: null },
        ],
        ...patch,
    }
}

export function summary(id = CID, name = 'Saturday open'): CompetitionSummary {
    return { id, name, fence_count: 12, entries: 3, runs: 0 }
}

/** Fake API pre-wired with a competition list and its state. */
export function apiWith(state: ArenaState, list: CompetitionSummary[] = [summary()]) {
    return new FakeApi()
        .on('GET', '/api/competitions/', { body: list })
        .on('GET', `/api/competitions/${state.competition.id}/state/`, { body: state })
}
