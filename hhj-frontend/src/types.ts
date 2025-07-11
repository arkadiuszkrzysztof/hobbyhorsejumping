export interface HobbyHorse {
    id: string
    name: string
    description?: string
}

export interface Participant {
    id: string
    name: string
    hobby_horses: HobbyHorse[]
}

export interface Judge {
    id: string
    name: string
}

export interface Course {
    id: string
    name: string
    description?: string
}

export interface Event {
    id: string
    name: string
    start_date: string
    end_date: string
    location: string
}

export interface Competition {
    id: string
    name: string
    event: Event
    start_date_time: string
    arena_size: string
    height_class: string
    time_limit: number
    description?: string
    course: Course
    judges: Judge[]
}

export interface CompetitionStart {
    id: string
    competition: Competition
    participant: Participant
    starting_order: number
    has_started: boolean
    has_finished: boolean
    is_paused?: boolean
    total_pause_time?: number
    pause_start_time?: number
    completion_time?: string
    penalty_points: number
    is_eliminated: boolean
    elimination_type?: string
    notes?: string
}

export interface TimeReading {
    id: string
    sensor_time: number
    server_time: number
    time_mark: 'START' | 'FINISH' | 'COMBINED' | 'FAULTY_READING'
    competition_start: string
}

export interface TimingData {
    id: string
    sensor_time: number
    server_time: number
    time_mark: string
    competition_start: CompetitionStart
}
