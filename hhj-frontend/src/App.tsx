import React, { useEffect, useRef, useState, useCallback } from 'react'
import useWebSocket, { ReadyState } from 'react-use-websocket'
import 'dotenv/config'

import { Box, Typography, Button, Table, TableBody, TableCell, TableContainer, TableHead, TableRow, Paper } from '@mui/material'

interface ParticipantSession {
    id: string
    name: string
    startTime: number
    endTime: number | null
    bestTime: number | null
    isActive: boolean
    signals: Array<{
        time_mark: 'START' | 'FINISH'
        server_time: number
    }>
}

interface AllParticipantSessions {
    [sessionId: string]: ParticipantSession
}

function App() {
    const [messageHistory, setMessageHistory] = useState<MessageEvent<any>[]>(
        []
    )
    const { sendMessage, lastMessage, readyState } = useWebSocket(
        `${process.env.REACT_APP_API_WEBSOCKET_URL}/ws/timings/`
    )

    const [startTime, setStartTime] = useState<number | null>(null)
    
    // Session management state
    const [currentSessionId, setCurrentSessionId] = useState<string | null>(null)
    const [allSessions, setAllSessions] = useState<AllParticipantSessions>({})
    const [hasStartSignal, setHasStartSignal] = useState<boolean>(false)
    const [isLoaded, setIsLoaded] = useState<boolean>(false) // Flag to prevent saving before loading

    // Load sessions from localStorage on component mount
    useEffect(() => {
        const savedSessions = localStorage.getItem('participantSessions')
        if (savedSessions) {
            try {
                const loadedSessions: AllParticipantSessions = JSON.parse(savedSessions)
                setAllSessions(loadedSessions)
                
                // Find if there's an active session and restore its state
                const activeSession = Object.values(loadedSessions).find((session: ParticipantSession) => session.isActive)
                if (activeSession) {
                    setCurrentSessionId(activeSession.id)
                    // Check if the active session has already started (has START signals)
                    const hasStart = activeSession.signals.some((s: any) => s.time_mark === 'START')
                    setHasStartSignal(hasStart)
                }
            } catch (error) {
                console.error('Error loading sessions from localStorage:', error)
            }
        }
        setIsLoaded(true) // Mark as loaded after attempting to load from localStorage
    }, [])

    // Save sessions to localStorage whenever allSessions changes (but only after initial load)
    useEffect(() => {
        if (isLoaded) {
            localStorage.setItem('participantSessions', JSON.stringify(allSessions))
        }
    }, [allSessions, isLoaded])

    useEffect(() => {
        if (lastMessage !== null) {
            setMessageHistory((prev) => prev.concat(lastMessage))

            const data = JSON.parse(lastMessage.data)

            // Only process signals if we have an active session
            if (currentSessionId) {
                setAllSessions(prev => {
                    const currentSession = prev[currentSessionId]
                    if (!currentSession || !currentSession.isActive) {
                        return prev // Don't process if session doesn't exist or is not active
                    }

                    const newSignal = {
                        time_mark: data.time_mark,
                        server_time: data.server_time
                    }

                    const updatedSessions = { ...prev }
                    const session = updatedSessions[currentSessionId]
                    const updatedSignals = [...session.signals, newSignal]
                    
                    // Calculate best time from signals
                    const startSignals = updatedSignals.filter(s => s.time_mark === 'START')
                    const finishSignals = updatedSignals.filter(s => s.time_mark === 'FINISH')
                    
                    let bestTime = null
                    if (startSignals.length > 0 && finishSignals.length > 0) {
                        // Use first start and last finish
                        const firstStart = startSignals[0].server_time
                        const lastFinish = finishSignals[finishSignals.length - 1].server_time
                        bestTime = lastFinish - firstStart
                    }
                    
                    return {
                        ...updatedSessions,
                        [currentSessionId]: {
                            ...session,
                            signals: updatedSignals,
                            bestTime
                        }
                    }
                })

                if (data.time_mark === 'START') {
                    // Take only the first start signal for timer
                    if (!hasStartSignal) {
                        resetTimer()
                        startTimer()
                        setStartTime(data.server_time)
                        setHasStartSignal(true)
                    }
                } else if (data.time_mark === 'FINISH' && hasStartSignal) {
                    // Calculate duration and update timer, then stop
                    const duration = data.server_time - startTime!
                    setTimer(duration)
                    stopTimer()
                }
            }
        }
    }, [lastMessage, currentSessionId, hasStartSignal, startTime])

    const connectionStatus = {
        [ReadyState.CONNECTING]: 'Connecting',
        [ReadyState.OPEN]: 'Open',
        [ReadyState.CLOSING]: 'Closing',
        [ReadyState.CLOSED]: 'Closed',
        [ReadyState.UNINSTANTIATED]: 'Uninstantiated',
    }[readyState]

    const [timer, setTimer] = useState(0)
    const interval = useRef<NodeJS.Timeout | null>(null)

    const minutes = Math.floor(timer / 60000)
    const seconds = Math.floor(timer / 1000) % 60
    const milliseconds = Math.floor((timer % 1000) / 10)

    const startTimer = () => {
        interval.current = setInterval(() => {
            setTimer((prev) => prev + 10)
        }, 10)
    }

    const stopTimer = () => {
        clearInterval(interval.current as NodeJS.Timeout)
    }

    const resetTimer = () => {
        setTimer(0)
        clearInterval(interval.current as NodeJS.Timeout)
    }



    // Session management functions
    const startNewParticipantSession = () => {
        const participantName = prompt('Enter participant name:')
        if (participantName && participantName.trim()) {
            const sessionId = Date.now().toString() + Math.random().toString(36).substr(2, 9)
            const newSession: ParticipantSession = {
                id: sessionId,
                name: participantName.trim(),
                startTime: Date.now(),
                endTime: null,
                bestTime: null,
                isActive: true,
                signals: []
            }
            
            setAllSessions(prev => ({
                ...prev,
                [sessionId]: newSession
            }))
            setCurrentSessionId(sessionId)
            setHasStartSignal(false)
            resetTimer()
        }
    }

    const endCurrentParticipantSession = () => {
        if (currentSessionId && allSessions[currentSessionId]) {
            const confirmEnd = window.confirm('Are you sure you want to end the current participant session?')
            if (confirmEnd) {
                setAllSessions(prev => ({
                    ...prev,
                    [currentSessionId]: {
                        ...prev[currentSessionId],
                        endTime: Date.now(),
                        isActive: false
                    }
                }))
                setCurrentSessionId(null)
                setHasStartSignal(false)
                resetTimer()
            }
        }
    }

    const resetCurrentParticipantSignals = () => {
        if (currentSessionId && allSessions[currentSessionId]) {
            const confirmReset = window.confirm('Are you sure you want to reset all signals for the current participant?')
            if (confirmReset) {
                setAllSessions(prev => ({
                    ...prev,
                    [currentSessionId]: {
                        ...prev[currentSessionId],
                        signals: [],
                        bestTime: null
                    }
                }))
                setHasStartSignal(false)
                resetTimer()
            }
        }
    }

    const resetAllParticipantSessions = () => {
        const confirmResetAll = window.confirm('Are you sure you want to reset ALL participant sessions? This cannot be undone.')
        if (confirmResetAll) {
            setAllSessions({})
            setCurrentSessionId(null)
            setHasStartSignal(false)
            resetTimer()
        }
    }

    // Get ranked participants (sorted by best time) - only include completed sessions
    const getRankedParticipants = () => {
        return Object.values(allSessions)
            .filter(session => session.bestTime !== null && !session.isActive)
            .sort((a, b) => a.bestTime! - b.bestTime!)
    }

    // Format time display
    const formatTime = (timeMs: number) => {
        const minutes = Math.floor(timeMs / 60000)
        const seconds = Math.floor(timeMs / 1000) % 60
        const milliseconds = Math.floor((timeMs % 1000) / 10)
        return `${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}.${milliseconds.toString().padStart(2, '0')}`
    }

    // Key bindings
    useEffect(() => {
        const handleKeyPress = (event: KeyboardEvent) => {
            if (event.key === 'Enter') {
                event.preventDefault()
                if (currentSessionId) {
                    endCurrentParticipantSession()
                } else {
                    startNewParticipantSession()
                }
            }
        }

        window.addEventListener('keydown', handleKeyPress)
        return () => window.removeEventListener('keydown', handleKeyPress)
    }, [currentSessionId])

    return (
        <Box sx={{ display: 'flex', height: '100vh' }}>
            {/* Left side - Rankings (25%) */}
            <Box sx={{ width: '25%', padding: 2, backgroundColor: '#f5f5f5' }}>
                <Typography variant="h4" sx={{ marginBottom: 2, textAlign: 'center' }}>
                    Rankings
                </Typography>
                <TableContainer component={Paper}>
                    <Table size="small">
                        <TableHead>
                            <TableRow>
                                <TableCell><strong>Rank</strong></TableCell>
                                <TableCell><strong>Name</strong></TableCell>
                                <TableCell><strong>Best Time</strong></TableCell>
                            </TableRow>
                        </TableHead>
                        <TableBody>
                            {getRankedParticipants().map((participant, index) => (
                                <TableRow key={participant.id}>
                                    <TableCell>{index + 1}</TableCell>
                                    <TableCell>{participant.name}</TableCell>
                                    <TableCell>{formatTime(participant.bestTime!)}</TableCell>
                                </TableRow>
                            ))}
                        </TableBody>
                    </Table>
                </TableContainer>
                
                {/* Current Session Info */}
                {currentSessionId && allSessions[currentSessionId] && (
                    <Box sx={{ marginTop: 2, padding: 2, backgroundColor: '#e3f2fd', borderRadius: 1 }}>
                        <Typography variant="h6" sx={{ color: '#1976d2' }}>
                            Current Participant: {allSessions[currentSessionId].name}
                        </Typography>
                        <Typography variant="body2">
                            Status: {hasStartSignal ? 'Started' : 'Waiting for start signal'}
                        </Typography>
                        <Typography variant="body2">
                            Signals: {allSessions[currentSessionId].signals.length} 
                            ({allSessions[currentSessionId].signals.filter(s => s.time_mark === 'START').length} start, {allSessions[currentSessionId].signals.filter(s => s.time_mark === 'FINISH').length} finish)
                        </Typography>
                        {allSessions[currentSessionId].bestTime && (
                            <Typography variant="body2">
                                Current Time: {formatTime(allSessions[currentSessionId].bestTime || 0)}
                            </Typography>
                        )}
                    </Box>
                )}
            </Box>

            {/* Right side - Timer and Controls (75%) */}
            <Box sx={{ width: '75%', display: 'flex', flexDirection: 'column' }}>
                {/* Top Bar - Current Participant (20% of right side) */}
                <Box sx={{ 
                    flex: '0 0 20%', 
                    display: 'flex', 
                    alignItems: 'center', 
                    justifyContent: 'center', 
                    backgroundColor: '#e8f5e8',
                    borderBottom: '2px solid #4caf50'
                }}>
                    <Typography
                        variant="h2"
                        sx={{
                            fontSize: '100px',
                            fontWeight: 'bold',
                            color: currentSessionId ? '#2e7d32' : '#757575',
                            textAlign: 'center'
                        }}
                    >
                        {currentSessionId && allSessions[currentSessionId] 
                            ? allSessions[currentSessionId].name 
                            : 'No Active Participant'
                        }
                    </Typography>
                </Box>

                {/* Timer (60% of right side) */}
                <Box sx={{ flex: '0 0 60%', display: 'flex', alignItems: 'center', justifyContent: 'center', textAlign: 'center' }}>
                    <Box sx={{ fontSize: '250px', fontFamily: 'monospace' }}>
                        {minutes.toString().padStart(2, '0')}:
                        {seconds.toString().padStart(2, '0')}
                        <Typography
                            sx={{
                                color: 'teal',
                                fontSize: '150px',
                                fontFamily: 'monospace',
                                display: 'inline',
                            }}
                        >
                            .{milliseconds.toString().padStart(2, '0')}
                        </Typography>
                    </Box>
                </Box>

                {/* Control Buttons (20% of right side) */}
                <Box sx={{ 
                    flex: '0 0 20%', 
                    display: 'flex', 
                    alignItems: 'center', 
                    justifyContent: 'center', 
                    gap: 2, 
                    padding: 2,
                    backgroundColor: '#f8f9fa'
                }}>
                    {/* Session Management Buttons */}
                    <Button
                        variant="contained"
                        size="large"
                        onClick={currentSessionId ? endCurrentParticipantSession : startNewParticipantSession}
                        sx={{ 
                            minWidth: 180, 
                            height: 80, 
                            fontSize: '16px',
                            backgroundColor: currentSessionId ? '#d32f2f' : '#2e7d32',
                            '&:hover': {
                                backgroundColor: currentSessionId ? '#c62828' : '#1b5e20'
                            }
                        }}
                    >
                        {currentSessionId ? 'End Session' : 'Start New Session'}
                        <br />
                        <small>(Enter)</small>
                    </Button>

                    <Button
                        variant="contained"
                        size="large"
                        onClick={resetTimer}
                        sx={{ 
                            minWidth: 180, 
                            height: 80, 
                            fontSize: '16px',
                            backgroundColor: '#757575',
                            '&:hover': {
                                backgroundColor: '#616161'
                            }
                        }}
                    >
                        Reset Timer
                    </Button>

                    {/* Reset Buttons */}
                    <Button
                        variant="contained"
                        size="large"
                        onClick={resetCurrentParticipantSignals}
                        disabled={!currentSessionId}
                        sx={{ 
                            minWidth: 180, 
                            height: 80, 
                            fontSize: '16px',
                            backgroundColor: '#ff5722',
                            '&:hover': {
                                backgroundColor: '#e64a19'
                            }
                        }}
                    >
                        Reset Current<br />Participant
                    </Button>

                    <Button
                        variant="contained"
                        size="large"
                        onClick={resetAllParticipantSessions}
                        sx={{ 
                            minWidth: 180, 
                            height: 80, 
                            fontSize: '16px',
                            backgroundColor: '#9c27b0',
                            '&:hover': {
                                backgroundColor: '#7b1fa2'
                            }
                        }}
                    >
                        Reset All<br />Sessions
                    </Button>
                </Box>
            </Box>
        </Box>
    )
}

export default App
