// Shapes from hhj-backend/API.md (the contract).

export type RunStatus = 'OPEN' | 'SAVED'

/** How a saved run ended. A retired or eliminated one has no time. */
export type Outcome = 'FINISHED' | 'RETIRED' | 'ELIMINATED'

/** Penalty points under the competition's rules: at the fences, for the time, and both. */
export interface Scored {
    fence_points: number
    time_points: number
    points: number
}

export interface Run extends Scored {
    id: string
    number: number
    status: RunStatus
    time_ms: number | null
    /** Knocked-down fences, by label: "7", "9B". */
    penalties: string[]
    /** Refusals per fence number: { "5": 2 }. */
    refusals: Record<string, number>
    outcome: Outcome
    /** Where a retired / eliminated round ended; '' = not said. */
    outcome_fence: string
}

export interface Entry {
    id: string
    name: string
    /** Name of the hobby horse; '' when none was given. */
    horse: string
    runs: Run[]
}

/** The scoring rules of a competition. */
export interface Rules {
    /** Seconds ("norma czasu"); null = no time penalties. */
    time_allowed: number | null
    knockdown_points: number
    refusal_points: number
    /** Per full second over the time allowed. */
    overtime_points: number
}

export interface Competition extends Rules {
    id: string
    name: string
    fence_count: number
}

export interface CompetitionSummary extends Competition {
    entries: number
    runs: number
}

export interface StartSignal {
    id: string
    sensor_time: number
    in_session: boolean
}

export interface FinishSignal extends StartSignal {
    time_ms: number | null
}

export interface Session extends Scored {
    run_id: string
    entry_id: string
    number: number
    capturing: boolean
    penalties: string[]
    refusals: Record<string, number>
    starts: StartSignal[]
    finishes: FinishSignal[]
    start_id: string | null
    finish_id: string | null
    start_auto: boolean
    finish_auto: boolean
    time_ms: number | null
}

export type DeviceRole = 'START' | 'FINISH' | 'TIMER'

export interface Device {
    role: DeviceRole
    online: boolean
    ok: boolean | null
}

export interface Busy {
    competition_id: string
    competition_name: string
}

export interface ArenaState {
    server_time: number
    competition: Competition
    entries: Entry[]
    selected_entry: string | null
    session: Session | null
    busy: Busy | null
    devices: Device[]
}

export type LiveMessage =
    | { type: 'state'; state: ArenaState }
    | { type: 'devices'; server_time: number; devices: Device[] }
