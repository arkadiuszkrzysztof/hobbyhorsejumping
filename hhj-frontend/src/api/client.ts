import type { ArenaState, CompetitionSummary } from './types'

/** status 0 = the request never got an answer (network down, server gone). */
export class ApiError extends Error {
    readonly status: number
    constructor(status: number, message: string) {
        super(message)
        this.status = status
    }
}

export type FetchFn = (input: string, init?: RequestInit) => Promise<Response>

export type PickBody = { start: string | null } | { finish: string | null }

export function createApi(fetchFn: FetchFn) {
    async function request<T>(method: string, path: string, body?: unknown): Promise<T> {
        let res: Response
        try {
            res = await fetchFn(path, {
                method,
                headers: body === undefined ? undefined : { 'Content-Type': 'application/json' },
                body: body === undefined ? undefined : JSON.stringify(body),
            })
        } catch {
            throw new ApiError(0, 'Cannot reach the server — check the WiFi / backend.')
        }
        let data: unknown = null
        const text = await res.text()
        if (text) {
            try {
                data = JSON.parse(text)
            } catch {
                data = null
            }
        }
        if (!res.ok) {
            const message =
                data && typeof data === 'object' && typeof (data as { error?: unknown }).error === 'string'
                    ? (data as { error: string }).error
                    : `Server error ${res.status}`
            throw new ApiError(res.status, message)
        }
        return data as T
    }

    const state = (method: string, path: string, body?: unknown) =>
        request<ArenaState>(method, path, body)

    return {
        listCompetitions: () => request<CompetitionSummary[]>('GET', '/api/competitions/'),
        createCompetition: (name: string, fence_count: number) =>
            state('POST', '/api/competitions/', { name, fence_count }),
        getState: (cid: string) => state('GET', `/api/competitions/${cid}/state/`),
        updateCompetition: (cid: string, patch: { name?: string; fence_count?: number }) =>
            state('PATCH', `/api/competitions/${cid}/`, patch),
        addEntry: (cid: string, name: string) =>
            state('POST', `/api/competitions/${cid}/entries/`, { name }),
        renameEntry: (eid: string, name: string) => state('PATCH', `/api/entries/${eid}/`, { name }),
        deleteEntry: (eid: string) => state('DELETE', `/api/entries/${eid}/`),
        reorder: (cid: string, entries: string[]) =>
            state('POST', `/api/competitions/${cid}/order/`, { entries }),
        select: (cid: string, entry: string | null, move: boolean) =>
            state('POST', `/api/competitions/${cid}/select/`, move ? { entry, move: true } : { entry }),
        capture: (cid: string, on: boolean) =>
            state('POST', `/api/competitions/${cid}/capture/`, { on }),
        penalty: (rid: string, fence: string) => state('POST', `/api/runs/${rid}/penalty/`, { fence }),
        pick: (rid: string, body: PickBody) => state('POST', `/api/runs/${rid}/pick/`, body),
        save: (rid: string) => state('POST', `/api/runs/${rid}/save/`),
        discard: (rid: string) => state('POST', `/api/runs/${rid}/discard/`),
        reopen: (rid: string) => state('POST', `/api/runs/${rid}/reopen/`),
        deleteRun: (rid: string) => state('DELETE', `/api/runs/${rid}/`),
    }
}

export type Api = ReturnType<typeof createApi>
