import { Competition, CompetitionStart } from './types'

const API_BASE_URL =
    process.env.REACT_APP_API_HTTP_URL || 'http://127.0.0.1:3003'

export const api = {
    // Get all competitions
    getCompetitions: async (): Promise<Competition[]> => {
        try {
            const response = await fetch(`${API_BASE_URL}/api/competitions/`)
            if (!response.ok) {
                throw new Error(
                    `HTTP ${response.status}: ${response.statusText}`
                )
            }
            return response.json()
        } catch (error) {
            if (error instanceof TypeError && error.message.includes('fetch')) {
                throw new Error(
                    `Cannot connect to backend at ${API_BASE_URL}. Make sure the Django server is running.`
                )
            }
            throw error
        }
    },

    // Get competition starts for a specific competition
    getCompetitionStarts: async (
        competitionId: string
    ): Promise<CompetitionStart[]> => {
        const response = await fetch(
            `${API_BASE_URL}/api/competition-starts/?competition_id=${competitionId}`
        )
        if (!response.ok) {
            throw new Error('Failed to fetch competition starts')
        }
        return response.json()
    },

    // Get the currently active competitor
    getActiveCompetitor: async (): Promise<CompetitionStart | null> => {
        try {
            const response = await fetch(
                `${API_BASE_URL}/api/active-competitor/`
            )
            if (!response.ok) {
                throw new Error(
                    `HTTP ${response.status}: ${response.statusText}`
                )
            }
            const data = await response.json()
            return data.active_competitor || null
        } catch (error) {
            if (error instanceof TypeError && error.message.includes('fetch')) {
                throw new Error(`Cannot connect to backend at ${API_BASE_URL}`)
            }
            throw error
        }
    },

    // Set the active competitor
    setActiveCompetitor: async (
        competitionStartId: string
    ): Promise<CompetitionStart> => {
        const response = await fetch(`${API_BASE_URL}/api/active-competitor/`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({ competition_start_id: competitionStartId }),
        })

        if (!response.ok) {
            throw new Error('Failed to set active competitor')
        }
        return response.json()
    },

    // Get timing readings
    getTimeReadings: async (): Promise<any[]> => {
        const response = await fetch(`${API_BASE_URL}/timereadings/`)
        if (!response.ok) {
            throw new Error('Failed to fetch time readings')
        }
        return response.json()
    },

    // Reorder competitors in a competition
    reorderCompetitors: async (
        competitionId: string,
        competitorOrders: { id: string; starting_order: number }[]
    ): Promise<CompetitionStart[]> => {
        const response = await fetch(
            `${API_BASE_URL}/api/reorder-competitors/`,
            {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({
                    competition_id: competitionId,
                    competitor_orders: competitorOrders,
                }),
            }
        )

        if (!response.ok) {
            throw new Error('Failed to reorder competitors')
        }
        return response.json()
    },

    // Get public display data
    getPublicDisplayData: async (): Promise<any> => {
        const response = await fetch(`${API_BASE_URL}/api/public-display/`)
        if (!response.ok) {
            throw new Error('Failed to fetch public display data')
        }
        return response.json()
    },

    // Reconcile timing signals
    reconcileTimingSignals: async (reconciliationData: {
        competitor_id: string
        start_signal: {
            sensor_time: number
            server_time: number
            timestamp: number
        }
        finish_signal: {
            sensor_time: number
            server_time: number
            timestamp: number
        }
    }): Promise<any> => {
        const response = await fetch(`${API_BASE_URL}/api/reconcile-signals/`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify(reconciliationData),
        })

        if (!response.ok) {
            throw new Error('Failed to reconcile timing signals')
        }
        return response.json()
    },

    // Get sensor status
    getSensorStatus: async (): Promise<any> => {
        const response = await fetch(`${API_BASE_URL}/sensor-status/`)
        if (!response.ok) {
            throw new Error('Failed to fetch sensor status')
        }
        return response.json()
    },

    // Pause or resume timer
    pauseResumeTimer: async (
        action: 'pause' | 'resume',
        currentTime: number
    ): Promise<any> => {
        const response = await fetch(`${API_BASE_URL}/api/pause-resume/`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({
                action,
                current_time: currentTime,
            }),
        })

        if (!response.ok) {
            throw new Error(`Failed to ${action} timer`)
        }
        return response.json()
    },

    // Manual timing trigger (keyboard)
    manualTiming: async (
        action: 'start' | 'finish',
        currentTime: number
    ): Promise<any> => {
        const response = await fetch(`${API_BASE_URL}/api/manual-timing/`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({
                action,
                current_time: currentTime,
            }),
        })

        if (!response.ok) {
            throw new Error(`Failed to trigger ${action}`)
        }
        return response.json()
    },
}
