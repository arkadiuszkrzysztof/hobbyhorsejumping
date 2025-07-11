import React, { useEffect, useCallback } from 'react'
import { Box, Chip, Typography, Stack } from '@mui/material'
import { PlayArrow, Pause, Timer, Flag, Stop } from '@mui/icons-material'
import { CompetitionStart } from '../types'
import { api } from '../api'

interface KeyboardControlsProps {
    activeCompetitor: CompetitionStart | null
    isRunning: boolean
    isPaused: boolean
    onPauseStateChange: (isPaused: boolean, totalPauseTime?: number) => void
    onTimingTrigger: (action: 'start' | 'finish') => void
}

const KeyboardControls: React.FC<KeyboardControlsProps> = ({
    activeCompetitor,
    isRunning,
    isPaused,
    onPauseStateChange,
    onTimingTrigger,
}) => {
    const handleKeyPress = useCallback(
        async (event: KeyboardEvent) => {
            // Only handle keyboard events when not in input fields
            if (
                event.target instanceof HTMLInputElement ||
                event.target instanceof HTMLTextAreaElement ||
                event.target instanceof HTMLSelectElement
            ) {
                return
            }

            // Prevent default behavior
            event.preventDefault()

            try {
                const currentTime = Date.now()

                switch (event.code) {
                    case 'Enter':
                        if (!activeCompetitor) {
                            console.warn('No active competitor selected')
                            return
                        }

                        if (!isRunning) {
                            // Start timing
                            await api.manualTiming('start', currentTime)
                            onTimingTrigger('start')
                        } else if (!isPaused) {
                            // Finish timing (only if running and not paused)
                            await api.manualTiming('finish', currentTime)
                            onTimingTrigger('finish')
                        }
                        break

                    case 'Space':
                        if (!activeCompetitor) {
                            console.warn('No active competitor selected')
                            return
                        }

                        if (isRunning) {
                            if (!isPaused) {
                                // Pause timer
                                await api.pauseResumeTimer('pause', currentTime)
                                onPauseStateChange(true)
                            } else {
                                // Resume timer
                                const response = await api.pauseResumeTimer(
                                    'resume',
                                    currentTime
                                )
                                onPauseStateChange(
                                    false,
                                    response.total_pause_time
                                )
                            }
                        }
                        break

                    default:
                        break
                }
            } catch (error) {
                console.error('Keyboard control error:', error)
            }
        },
        [
            activeCompetitor,
            isRunning,
            isPaused,
            onPauseStateChange,
            onTimingTrigger,
        ]
    )

    useEffect(() => {
        // Add event listener for keyboard events
        document.addEventListener('keydown', handleKeyPress)

        // Cleanup event listener on unmount
        return () => {
            document.removeEventListener('keydown', handleKeyPress)
        }
    }, [handleKeyPress])

    const getTimerStatusIcon = () => {
        if (isPaused) return <Pause />
        if (isRunning) return <Timer />
        return <Stop />
    }

    const getTimerStatusText = () => {
        if (isPaused) return 'Timer Paused'
        if (isRunning) return 'Timer Running'
        return 'Timer Stopped'
    }

    const getTimerStatusColor = ():
        | 'default'
        | 'primary'
        | 'secondary'
        | 'error'
        | 'info'
        | 'success'
        | 'warning' => {
        if (isPaused) return 'warning'
        if (isRunning) return 'success'
        return 'default'
    }

    return (
        <Box
            sx={{
                p: 2,
                border: 1,
                borderColor: 'divider',
                borderRadius: 1,
                bgcolor: 'background.paper',
                mb: 2,
            }}
        >
            <Typography variant="h6" gutterBottom>
                ⌨️ Keyboard Controls
            </Typography>

            <Stack
                direction="row"
                spacing={2}
                alignItems="center"
                sx={{ mb: 2 }}
            >
                <Chip
                    icon={getTimerStatusIcon()}
                    label={getTimerStatusText()}
                    color={getTimerStatusColor()}
                    variant="outlined"
                />

                {activeCompetitor ? (
                    <Chip
                        label={`Active: ${activeCompetitor.participant.name}`}
                        color="primary"
                        variant="outlined"
                    />
                ) : (
                    <Chip
                        label="No Active Competitor"
                        color="default"
                        variant="outlined"
                    />
                )}
            </Stack>

            <Stack spacing={1}>
                <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
                    <Chip
                        label="Enter"
                        size="small"
                        variant="outlined"
                        sx={{ minWidth: 60 }}
                    />
                    <Typography variant="body2" color="text.secondary">
                        {!isRunning ? (
                            <>
                                <PlayArrow
                                    sx={{
                                        fontSize: 16,
                                        verticalAlign: 'middle',
                                        mr: 0.5,
                                    }}
                                />
                                Start timing
                            </>
                        ) : !isPaused ? (
                            <>
                                <Flag
                                    sx={{
                                        fontSize: 16,
                                        verticalAlign: 'middle',
                                        mr: 0.5,
                                    }}
                                />
                                Finish timing
                            </>
                        ) : (
                            'Resume first to finish'
                        )}
                    </Typography>
                </Box>

                <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
                    <Chip
                        label="Space"
                        size="small"
                        variant="outlined"
                        sx={{ minWidth: 60 }}
                    />
                    <Typography variant="body2" color="text.secondary">
                        {isRunning ? (
                            isPaused ? (
                                <>
                                    <PlayArrow
                                        sx={{
                                            fontSize: 16,
                                            verticalAlign: 'middle',
                                            mr: 0.5,
                                        }}
                                    />
                                    Resume timer
                                </>
                            ) : (
                                <>
                                    <Pause
                                        sx={{
                                            fontSize: 16,
                                            verticalAlign: 'middle',
                                            mr: 0.5,
                                        }}
                                    />
                                    Pause timer
                                </>
                            )
                        ) : (
                            'Start timing first'
                        )}
                    </Typography>
                </Box>
            </Stack>

            {activeCompetitor?.total_pause_time &&
                activeCompetitor.total_pause_time > 0 && (
                    <Box
                        sx={{
                            mt: 1,
                            p: 1,
                            bgcolor: 'warning.light',
                            borderRadius: 1,
                        }}
                    >
                        <Typography variant="caption" color="warning.dark">
                            Total pause time:{' '}
                            {(activeCompetitor.total_pause_time / 1000).toFixed(
                                2
                            )}
                            s
                        </Typography>
                    </Box>
                )}
        </Box>
    )
}

export default KeyboardControls
