import { ApiError, createApi, type Api, type FetchFn, type PickBody } from '../api/client'
import type { ArenaState, CompetitionSummary, Device, LiveMessage } from '../api/types'
import { nextClockOffset } from '../lib/time'

/** The bits of the browser the store touches — injected so tests can fake them. */
export interface WebSocketLike {
    onopen: ((ev: Event) => void) | null
    onmessage: ((ev: MessageEvent) => void) | null
    onclose: ((ev: CloseEvent) => void) | null
    onerror: ((ev: Event) => void) | null
    close(): void
}

export interface StoreDeps {
    fetch: FetchFn
    createWebSocket: (url: string) => WebSocketLike
    location: { protocol: string; host: string }
    storage: Pick<Storage, 'getItem' | 'setItem' | 'removeItem'> | null
    now: () => number
    setTimeout: (fn: () => void, ms: number) => unknown
    clearTimeout: (handle: unknown) => void
}

export type Phase = 'loading' | 'unreachable' | 'first-run' | 'ready'
export type Connection = 'connecting' | 'online' | 'offline'

export interface AppError {
    id: number
    message: string
}

export interface Snapshot {
    phase: Phase
    competitions: CompetitionSummary[]
    competitionId: string | null
    state: ArenaState | null
    connection: Connection
    pending: ReadonlySet<string>
    errors: AppError[]
    /** Estimated server clock minus local clock (ms), null until a state arrived. */
    clockOffset: number | null
}

export type ActionResult =
    | { ok: true; state: ArenaState }
    | { ok: false; status: number; error: string }

export const STORAGE_KEY = 'hhj.competition'
const RECONNECT_BASE_MS = 500
const RECONNECT_MAX_MS = 10_000

export function reconnectDelay(attempt: number): number {
    return Math.min(RECONNECT_MAX_MS, RECONNECT_BASE_MS * 2 ** attempt)
}

export class CompetitionStore {
    private snap: Snapshot = {
        phase: 'loading',
        competitions: [],
        competitionId: null,
        state: null,
        connection: 'connecting',
        pending: new Set(),
        errors: [],
        clockOffset: null,
    }
    private readonly listeners = new Set<() => void>()
    private readonly api: Api
    private ws: WebSocketLike | null = null
    private reconnectTimer: unknown = null
    private attempt = 0
    private refetchOnOpen = false
    private devicesTime = -Infinity
    private nextErrorId = 1
    private disposed = false
    private generation = 0

    constructor(private readonly deps: StoreDeps) {
        this.api = createApi(deps.fetch)
    }

    // ---- external-store plumbing (useSyncExternalStore) ----

    subscribe = (fn: () => void) => {
        this.listeners.add(fn)
        return () => {
            this.listeners.delete(fn)
        }
    }

    getSnapshot = () => this.snap

    private set(patch: Partial<Snapshot>) {
        this.snap = { ...this.snap, ...patch }
        this.listeners.forEach((fn) => fn())
    }

    // ---- lifecycle ----

    /** Load the competition list and open the remembered / newest one. */
    async start(): Promise<void> {
        const gen = ++this.generation
        this.disposed = false
        this.set({ phase: 'loading' })
        let list: CompetitionSummary[]
        try {
            list = await this.api.listCompetitions()
        } catch (e) {
            if (gen !== this.generation) return
            this.set({ phase: 'unreachable' })
            this.pushError(errorMessage(e))
            return
        }
        if (gen !== this.generation) return
        this.set({ competitions: list })
        if (list.length === 0) {
            this.set({ phase: 'first-run' })
            return
        }
        const remembered = this.deps.storage?.getItem(STORAGE_KEY) ?? null
        const chosen = list.find((c) => c.id === remembered) ?? list[0]!
        this.set({ phase: 'ready' })
        this.open(chosen.id)
    }

    dispose() {
        this.generation++
        this.disposed = true
        this.closeSocket()
    }

    /** Switch to a competition: fetch its state and follow it live. */
    open(cid: string) {
        this.closeSocket()
        this.attempt = 0
        this.refetchOnOpen = false
        this.devicesTime = -Infinity
        this.deps.storage?.setItem(STORAGE_KEY, cid)
        this.set({ competitionId: cid, state: null, connection: 'connecting', phase: 'ready' })
        void this.fetchState()
        this.connect()
    }

    async refreshCompetitions() {
        try {
            const list = await this.api.listCompetitions()
            this.set({ competitions: list })
        } catch (e) {
            this.pushError(errorMessage(e))
        }
    }

    private async fetchState() {
        const cid = this.snap.competitionId
        if (!cid) return
        try {
            this.applyState(await this.api.getState(cid))
        } catch (e) {
            if (e instanceof ApiError && e.status === 404 && cid === this.snap.competitionId) {
                // Competition is gone (deleted in the admin?) — fall back to the list.
                this.deps.storage?.removeItem(STORAGE_KEY)
                this.closeSocket()
                this.pushError(e.message)
                void this.start()
                return
            }
            this.pushError(errorMessage(e))
        }
    }

    // ---- WebSocket ----

    private connect() {
        const cid = this.snap.competitionId
        if (!cid || this.disposed) return
        const scheme = this.deps.location.protocol === 'https:' ? 'wss' : 'ws'
        const ws = this.deps.createWebSocket(`${scheme}://${this.deps.location.host}/ws/live/${cid}/`)
        this.ws = ws
        ws.onopen = () => {
            if (this.ws !== ws) return
            this.attempt = 0
            this.set({ connection: 'online' })
            if (this.refetchOnOpen) {
                this.refetchOnOpen = false
                void this.fetchState()
            }
        }
        ws.onmessage = (ev) => {
            if (this.ws !== ws) return
            let msg: LiveMessage
            try {
                msg = JSON.parse(String(ev.data)) as LiveMessage
            } catch {
                return
            }
            if (msg.type === 'state') this.applyState(msg.state)
            else if (msg.type === 'devices') this.applyDevices(msg.server_time, msg.devices)
        }
        ws.onerror = () => {
            /* a close event always follows */
        }
        ws.onclose = () => {
            if (this.ws !== ws) return
            this.ws = null
            this.refetchOnOpen = true
            this.set({ connection: 'offline' })
            this.scheduleReconnect()
        }
    }

    private scheduleReconnect() {
        if (this.disposed) return
        const delay = reconnectDelay(this.attempt)
        this.attempt += 1
        this.reconnectTimer = this.deps.setTimeout(() => {
            this.reconnectTimer = null
            this.connect()
        }, delay)
    }

    private closeSocket() {
        if (this.reconnectTimer !== null) {
            this.deps.clearTimeout(this.reconnectTimer)
            this.reconnectTimer = null
        }
        const ws = this.ws
        this.ws = null // handlers check identity, so the old socket goes quiet
        ws?.close()
    }

    // ---- applying server data ----

    /** Apply a state if it belongs to the open competition and is not older than what we show. */
    applyState(next: ArenaState) {
        const clockOffset = nextClockOffset(this.snap.clockOffset, next.server_time, this.deps.now())
        if (next.competition.id !== this.snap.competitionId) {
            this.set({ clockOffset })
            return
        }
        const cur = this.snap.state
        if (cur && next.server_time < cur.server_time) {
            this.set({ clockOffset })
            return
        }
        // Device lights may have been refreshed by a newer "devices" push.
        const state =
            cur && this.devicesTime > next.server_time ? { ...next, devices: cur.devices } : next
        this.set({ state, clockOffset })
        const known = this.snap.competitions.find((c) => c.id === next.competition.id)
        if (
            known &&
            (known.name !== next.competition.name || known.fence_count !== next.competition.fence_count)
        ) {
            this.set({
                competitions: this.snap.competitions.map((c) =>
                    c.id === next.competition.id ? { ...c, ...next.competition } : c
                ),
            })
        }
    }

    private applyDevices(serverTime: number, devices: Device[]) {
        const clockOffset = nextClockOffset(this.snap.clockOffset, serverTime, this.deps.now())
        const cur = this.snap.state
        if (!cur || serverTime < this.devicesTime || serverTime < cur.server_time) {
            this.set({ clockOffset })
            return
        }
        this.devicesTime = serverTime
        this.set({ state: { ...cur, devices }, clockOffset })
    }

    // ---- errors ----

    pushError(message: string) {
        const errors = this.snap.errors.filter((e) => e.message !== message)
        this.set({ errors: [...errors, { id: this.nextErrorId++, message }].slice(-4) })
    }

    dismissError(id: number) {
        this.set({ errors: this.snap.errors.filter((e) => e.id !== id) })
    }

    // ---- actions ----

    private async perform(
        key: string,
        call: () => Promise<ArenaState>,
        quietStatuses: number[] = []
    ): Promise<ActionResult> {
        if (this.snap.pending.has(key)) return { ok: false, status: -1, error: 'already in progress' }
        this.setPending(key, true)
        try {
            const state = await call()
            this.applyState(state)
            return { ok: true, state }
        } catch (e) {
            const status = e instanceof ApiError ? e.status : 0
            const error = errorMessage(e)
            if (!quietStatuses.includes(status)) this.pushError(error)
            return { ok: false, status, error }
        } finally {
            this.setPending(key, false)
        }
    }

    private setPending(key: string, on: boolean) {
        const pending = new Set(this.snap.pending)
        if (on) pending.add(key)
        else pending.delete(key)
        this.set({ pending })
    }

    private get cid(): string {
        return this.snap.competitionId ?? ''
    }

    private get runId(): string | null {
        return this.snap.state?.session?.run_id ?? null
    }

    private withRun(key: string, call: (rid: string) => Promise<ArenaState>) {
        const rid = this.runId
        if (!rid) return Promise.resolve<ActionResult>({ ok: false, status: -1, error: 'no session' })
        return this.perform(key, () => call(rid))
    }

    async createCompetition(name: string, fenceCount: number): Promise<ActionResult> {
        const result = await this.perform('competition:create', () =>
            this.api.createCompetition(name, fenceCount)
        )
        if (result.ok) {
            await this.refreshCompetitions()
            this.open(result.state.competition.id)
            this.applyState(result.state)
        }
        return result
    }

    updateCompetition(patch: { name?: string; fence_count?: number }) {
        return this.perform('competition:update', () => this.api.updateCompetition(this.cid, patch))
    }

    addEntry(name: string) {
        return this.perform('entry:add', () => this.api.addEntry(this.cid, name))
    }

    renameEntry(eid: string, name: string) {
        return this.perform(`entry:rename:${eid}`, () => this.api.renameEntry(eid, name))
    }

    removeEntry(eid: string) {
        return this.perform(`entry:remove:${eid}`, () => this.api.deleteEntry(eid))
    }

    reorder(ids: string[]) {
        return this.perform('order', () => this.api.reorder(this.cid, ids))
    }

    /** A 409 is returned quietly: the UI asks whether to move the open session. */
    select(eid: string | null, move = false) {
        return this.perform('select', () => this.api.select(this.cid, eid, move), move ? [] : [409])
    }

    capture(on: boolean) {
        return this.perform('capture', () => this.api.capture(this.cid, on))
    }

    penalty(fence: string) {
        return this.withRun(`penalty:${fence}`, (rid) => this.api.penalty(rid, fence))
    }

    pick(body: PickBody) {
        return this.withRun('pick', (rid) => this.api.pick(rid, body))
    }

    save() {
        return this.withRun('save', (rid) => this.api.save(rid))
    }

    discard() {
        return this.withRun('discard', (rid) => this.api.discard(rid))
    }

    reopen(rid: string) {
        return this.perform(`run:${rid}`, () => this.api.reopen(rid))
    }

    deleteRun(rid: string) {
        return this.perform(`run:${rid}`, () => this.api.deleteRun(rid))
    }
}

function errorMessage(e: unknown): string {
    if (e instanceof Error) return e.message
    return String(e)
}

export function browserDeps(): StoreDeps {
    let storage: StoreDeps['storage'] = null
    try {
        storage = window.localStorage
    } catch {
        storage = null
    }
    return {
        fetch: (input, init) => window.fetch(input, init),
        createWebSocket: (url) => new WebSocket(url),
        location: window.location,
        storage,
        now: () => Date.now(),
        setTimeout: (fn, ms) => window.setTimeout(fn, ms),
        clearTimeout: (h) => window.clearTimeout(h as number),
    }
}
