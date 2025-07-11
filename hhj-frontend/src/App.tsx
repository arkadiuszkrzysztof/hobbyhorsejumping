import React, { useEffect, useRef, useState } from 'react'
import useWebSocket, { ReadyState } from 'react-use-websocket'
import 'dotenv/config'

import {
    Box,
    Grid,
    Alert,
    Snackbar,
    Container,
    AppBar,
    Toolbar,
    Typography,
    IconButton,
    Tooltip,
} from '@mui/material'
import { BugReportOutlined, Reorder } from '@mui/icons-material'
import { Competition, CompetitionStart } from './types'
import { api } from './api'
import CompetitionSelector from './components/CompetitionSelector'
import CompetitorList from './components/CompetitorList'
import TimerDisplay from './components/TimerDisplay'
import StatusDisplay from './components/StatusDisplay'
import SimpleTestPage from './components/SimpleTestPage'
import ConnectionTest from './components/ConnectionTest'
import CompetitorReorder from './components/CompetitorReorder'
import SignalTracking from './components/SignalTracking'
import SensorStatusWidget from './components/SensorStatusWidget'
import KeyboardControls from './components/KeyboardControls'

function App() {
    // WebSocket connection
    const [messageHistory, setMessageHistory] = useState<MessageEvent<any>[]>(
        []
    )
    const { lastMessage, readyState } = useWebSocket(
        `${process.env.REACT_APP_API_WEBSOCKET_URL}/ws/timings/`
    )

    // Competition and competitor state
    const [competitions, setCompetitions] = useState<Competition[]>([])
    const [selectedCompetition, setSelectedCompetition] =
        useState<Competition | null>(null)
    const [competitionStarts, setCompetitionStarts] = useState<
        CompetitionStart[]
    >([])
    const [activeCompetitor, setActiveCompetitor] =
        useState<CompetitionStart | null>(null)

    // Timer state
    const [timer, setTimer] = useState(0)
    const [startTime, setStartTime] = useState<number | null>(null)
    const [isRunning, setIsRunning] = useState(false)
    const [isPaused, setIsPaused] = useState(false)
    const [totalPauseTime, setTotalPauseTime] = useState(0)
    const interval = useRef<NodeJS.Timeout | null>(null)

    // UI state
    const [error, setError] = useState<string | null>(null)
    const [loading, setLoading] = useState(true)
    const [showTestPage, setShowTestPage] = useState(false)
    const [showReorderPage, setShowReorderPage] = useState(false)

    // Store timings for each competitor
    const [competitorTimings, setCompetitorTimings] = useState<{
        [key: string]: { start?: number; finish?: number; elapsed?: number }
    }>({})

    // Load initial data
    useEffect(() => {
        const loadInitialData = async () => {
            try {
                setLoading(true)

                // Load competitions
                const competitionsData = await api.getCompetitions()
                setCompetitions(competitionsData)

                // If there's only one competition, select it automatically
                if (competitionsData.length === 1) {
                    await handleCompetitionSelect(competitionsData[0])
                }

                // Load active competitor if any
                const activeComp = await api.getActiveCompetitor()
                if (activeComp) {
                    setActiveCompetitor(activeComp)
                    // If we have an active competitor, load their competition
                    if (!selectedCompetition) {
                        await handleCompetitionSelect(activeComp.competition)
                    }
                }
            } catch (err) {
                const errorMessage =
                    err instanceof Error
                        ? `Failed to load initial data: ${err.message}`
                        : 'Failed to load initial data. Please check your backend connection.'
                setError(errorMessage)
                console.error('Failed to load initial data:', err)
            } finally {
                setLoading(false)
            }
        }

        loadInitialData()
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [])

    // Handle competition selection
    const handleCompetitionSelect = async (competition: Competition) => {
        try {
            setSelectedCompetition(competition)
            const starts = await api.getCompetitionStarts(competition.id)
            setCompetitionStarts(starts)
        } catch (err) {
            setError('Failed to load competition participants')
            console.error('Failed to load competition starts:', err)
        }
    }

    // Handle competitor selection
    const handleCompetitorSelect = async (
        competitionStart: CompetitionStart
    ) => {
        try {
            const updatedCompetitor = await api.setActiveCompetitor(
                competitionStart.id
            )
            setActiveCompetitor(updatedCompetitor)

            // Reset timer state for new competitor
            resetTimer()

            setError(null)
        } catch (err) {
            setError('Failed to set active competitor')
            console.error('Failed to set active competitor:', err)
        }
    }

    // Handle competitor reordering
    const handleCompetitorReorder = async (
        reorderedCompetitors: CompetitionStart[]
    ) => {
        if (!selectedCompetition) return

        try {
            const competitorOrders = reorderedCompetitors.map((competitor) => ({
                id: competitor.id,
                starting_order: competitor.starting_order,
            }))

            const updatedStarts = await api.reorderCompetitors(
                selectedCompetition.id,
                competitorOrders
            )

            setCompetitionStarts(updatedStarts)
            setShowReorderPage(false)
            setError(null)
        } catch (err) {
            setError('Failed to reorder competitors')
            console.error('Failed to reorder competitors:', err)
        }
    }

    // Timer functions
    const startTimer = () => {
        if (!isRunning && !isPaused) {
            setIsRunning(true)
            setIsPaused(false)
            interval.current = setInterval(() => {
                setTimer((prev) => prev + 10)
            }, 10)
        }
    }

    const stopTimer = () => {
        setIsRunning(false)
        setIsPaused(false)
        if (interval.current) {
            clearInterval(interval.current)
            interval.current = null
        }
    }

    const pauseTimer = () => {
        if (isRunning && !isPaused) {
            setIsPaused(true)
            if (interval.current) {
                clearInterval(interval.current)
                interval.current = null
            }
        }
    }

    const resumeTimer = () => {
        if (isRunning && isPaused) {
            setIsPaused(false)
            interval.current = setInterval(() => {
                setTimer((prev) => prev + 10)
            }, 10)
        }
    }

    const resetTimer = () => {
        stopTimer()
        setTimer(0)
        setStartTime(null)
        setTotalPauseTime(0)
    }

    // Keyboard control handlers
    const handlePauseStateChange = (
        paused: boolean,
        newTotalPauseTime?: number
    ) => {
        if (paused) {
            pauseTimer()
        } else {
            resumeTimer()
            if (newTotalPauseTime !== undefined) {
                setTotalPauseTime(newTotalPauseTime)
            }
        }
    }

    const handleTimingTrigger = (action: 'start' | 'finish') => {
        // The actual timing will be handled by WebSocket messages
        // This is just for UI feedback
        if (action === 'start') {
            resetTimer()
            startTimer()
        } else if (action === 'finish') {
            stopTimer()
        }
    }

    // Handle WebSocket messages
    useEffect(() => {
        if (lastMessage !== null) {
            setMessageHistory((prev) => prev.concat(lastMessage))

            try {
                const message = JSON.parse(lastMessage.data)

                // Handle different message types
                if (message.type === 'state_update') {
                    // Update active competitor from backend state
                    if (message.active_competitor) {
                        setActiveCompetitor(message.active_competitor)
                    }
                } else if (message.type === 'timer_paused') {
                    // Handle timer pause from backend
                    if (message.active_competitor) {
                        setActiveCompetitor(message.active_competitor)
                    }
                    pauseTimer()
                } else if (message.type === 'timer_resumed') {
                    // Handle timer resume from backend
                    if (message.active_competitor) {
                        setActiveCompetitor(message.active_competitor)
                        setTotalPauseTime(message.total_pause_time || 0)
                    }
                    resumeTimer()
                } else if (
                    message.type === 'competition_update' ||
                    message.type === 'competitor_reorder'
                ) {
                    // Reload competition data when reordering occurs
                    if (selectedCompetition) {
                        api.getCompetitionStarts(selectedCompetition.id)
                            .then((starts) => setCompetitionStarts(starts))
                            .catch(console.error)
                    }
                } else if (message.time_mark) {
                    // Direct timing data (from sensors or tests)
                    const data = message

                    if (data.time_mark === 'START') {
                        resetTimer()
                        startTimer()
                        const sensorTime =
                            typeof data.sensor_time === 'string'
                                ? parseInt(data.sensor_time)
                                : data.sensor_time
                        setStartTime(sensorTime)

                        // Update competitor timings
                        if (activeCompetitor) {
                            setCompetitorTimings((prev) => ({
                                ...prev,
                                [activeCompetitor.id]: {
                                    ...prev[activeCompetitor.id],
                                    start: sensorTime,
                                },
                            }))
                        }
                    } else if (data.time_mark === 'FINISH' && startTime) {
                        stopTimer()
                        const sensorTime =
                            typeof data.sensor_time === 'string'
                                ? parseInt(data.sensor_time)
                                : data.sensor_time
                        const elapsedTime = sensorTime - startTime
                        setTimer(elapsedTime)

                        // Update competitor timings and mark as finished
                        if (activeCompetitor) {
                            setCompetitorTimings((prev) => ({
                                ...prev,
                                [activeCompetitor.id]: {
                                    ...prev[activeCompetitor.id],
                                    finish: sensorTime,
                                    elapsed: elapsedTime,
                                },
                            }))

                            // Update the competitor's finished status
                            setActiveCompetitor((prev) =>
                                prev
                                    ? {
                                          ...prev,
                                          has_finished: true,
                                      }
                                    : null
                            )

                            // Update in the competition starts list
                            setCompetitionStarts((prev) =>
                                prev.map((cs) =>
                                    cs.id === activeCompetitor.id
                                        ? { ...cs, has_finished: true }
                                        : cs
                                )
                            )
                        }
                    }
                }
            } catch (err) {
                console.error('Failed to parse WebSocket message:', err)
            }
        }
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [lastMessage, startTime, activeCompetitor, selectedCompetition])

    if (loading) {
        return (
            <Container maxWidth="lg">
                <Box
                    sx={{
                        display: 'flex',
                        justifyContent: 'center',
                        alignItems: 'center',
                        height: '100vh',
                    }}
                >
                    <Typography variant="h6">Loading...</Typography>
                </Box>
            </Container>
        )
    }

    // Show test page if enabled
    if (showTestPage) {
        return <SimpleTestPage onClose={() => setShowTestPage(false)} />
    }

    // Show reorder page if enabled
    if (showReorderPage && selectedCompetition) {
        return (
            <CompetitorReorder
                competitionStarts={competitionStarts}
                onReorder={handleCompetitorReorder}
                onClose={() => setShowReorderPage(false)}
                disabled={activeCompetitor?.has_started || false}
            />
        )
    }

    return (
        <Box sx={{ flexGrow: 1, bgcolor: 'grey.50', minHeight: '100vh' }}>
            {/* App Bar */}
            <AppBar position="static" elevation={1}>
                <Toolbar>
                    <Typography
                        variant="h6"
                        component="div"
                        sx={{ flexGrow: 1 }}
                    >
                        🐎 Hobby Horse Jumping Championship - Timing System
                    </Typography>
                    <Tooltip title="Reorder Starting List">
                        <IconButton
                            color="inherit"
                            onClick={() => setShowReorderPage(true)}
                            disabled={
                                !selectedCompetition ||
                                competitionStarts.length === 0
                            }
                            sx={{ mr: 1 }}
                        >
                            <Reorder />
                        </IconButton>
                    </Tooltip>
                    <Tooltip title="Open Testing Interface">
                        <IconButton
                            color="inherit"
                            onClick={() => setShowTestPage(true)}
                            sx={{ mr: 2 }}
                        >
                            <BugReportOutlined />
                        </IconButton>
                    </Tooltip>
                    <Typography variant="body2" color="inherit">
                        {selectedCompetition?.name || 'No competition selected'}
                    </Typography>
                </Toolbar>
            </AppBar>

            <Container maxWidth="xl" sx={{ py: 3 }}>
                {/* Connection Test - Show when there's an error */}
                {error && <ConnectionTest />}

                {/* Competition Selector */}
                {competitions.length > 1 && (
                    <CompetitionSelector
                        competitions={competitions}
                        selectedCompetition={selectedCompetition}
                        onSelectCompetition={handleCompetitionSelect}
                    />
                )}

                {/* Sensor and Connection Status */}
                <SensorStatusWidget
                    connectionStatus={readyState}
                    messageHistory={messageHistory}
                />

                {/* Status Display */}
                <StatusDisplay
                    connectionStatus={readyState}
                    activeCompetitor={activeCompetitor}
                    lastMessage={lastMessage}
                    messageHistory={messageHistory}
                />

                {/* Keyboard Controls */}
                <KeyboardControls
                    activeCompetitor={activeCompetitor}
                    isRunning={isRunning}
                    isPaused={isPaused}
                    onPauseStateChange={handlePauseStateChange}
                    onTimingTrigger={handleTimingTrigger}
                />

                {/* Signal Tracking Widget */}
                <SignalTracking
                    activeCompetitor={activeCompetitor}
                    messageHistory={messageHistory}
                />

                {/* Main Layout */}
                <Grid container spacing={3}>
                    {/* Competitor List */}
                    <Grid item xs={12} md={4} lg={3}>
                        <CompetitorList
                            competitionStarts={competitionStarts}
                            activeCompetitor={activeCompetitor}
                            onSelectCompetitor={handleCompetitorSelect}
                            timings={competitorTimings}
                        />
                    </Grid>

                    {/* Timer Display */}
                    <Grid item xs={12} md={8} lg={9}>
                        <TimerDisplay
                            timer={timer}
                            isRunning={isRunning}
                            isPaused={isPaused}
                            totalPauseTime={totalPauseTime}
                            activeCompetitor={activeCompetitor}
                            competitionTimeLimit={
                                selectedCompetition?.time_limit
                            }
                        />
                    </Grid>
                </Grid>
            </Container>

            {/* Error Snackbar */}
            <Snackbar
                open={!!error}
                autoHideDuration={6000}
                onClose={() => setError(null)}
            >
                <Alert
                    severity="error"
                    onClose={() => setError(null)}
                    sx={{ width: '100%' }}
                >
                    {error}
                </Alert>
            </Snackbar>
        </Box>
    )
}

export default App
