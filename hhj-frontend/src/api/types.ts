// Shapes from hhj-backend/API.md (the contract).

export type RunStatus = 'OPEN' | 'SAVED'

export interface Run {
    id: string
    number: number
    status: RunStatus
    time_ms: number | null
    penalties: string[]
}

export interface Entry {
    id: string
    name: string
    /** Name of the hobby horse; '' when none was given. */
    horse: string
    runs: Run[]
}

export interface Competition {
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

export interface Session {
    run_id: string
    entry_id: string
    number: number
    capturing: boolean
    penalties: string[]
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
