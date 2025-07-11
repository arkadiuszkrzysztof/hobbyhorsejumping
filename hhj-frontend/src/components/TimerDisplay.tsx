import React from 'react'
import { Box, Typography, Paper, Fade, Chip } from '@mui/material'
import {
    PlayArrowOutlined,
    StopOutlined,
    FlagOutlined,
} from '@mui/icons-material'
import { CompetitionStart } from '../types'

interface TimerDisplayProps {
    timer: number
    isRunning: boolean
    isPaused?: boolean
    totalPauseTime?: number
    activeCompetitor: CompetitionStart | null
    competitionTimeLimit?: number
}

const TimerDisplay: React.FC<TimerDisplayProps> = ({
    timer,
    isRunning,
    isPaused = false,
    totalPauseTime = 0,
    activeCompetitor,
    competitionTimeLimit,
}) => {
    const minutes = Math.floor(timer / 60000)
    const seconds = Math.floor((timer % 60000) / 1000)
    const milliseconds = Math.floor((timer % 1000) / 10)

    const isOverTime =
        competitionTimeLimit && timer > competitionTimeLimit * 1000
    const timeColor = isOverTime ? 'error.main' : 'text.primary'
    const millisecondsColor = isOverTime ? 'error.main' : 'teal'

    const getStatusIcon = () => {
        if (!activeCompetitor) return null
        if (activeCompetitor.has_finished)
            return <FlagOutlined sx={{ fontSize: 60, color: 'success.main' }} />
        if (isRunning)
            return (
                <PlayArrowOutlined
                    sx={{ fontSize: 60, color: 'primary.main' }}
                />
            )
        return <StopOutlined sx={{ fontSize: 60, color: 'warning.main' }} />
    }

    const getStatusText = () => {
        if (!activeCompetitor) return 'Select a competitor to start timing'
        if (activeCompetitor.has_finished) return 'Finished!'
        if (isRunning) return 'Timing in progress...'
        return 'Ready to start'
    }

    return (
        <Paper
            elevation={3}
            sx={{
                p: 4,
                textAlign: 'center',
                bgcolor: isOverTime ? 'error.light' : 'background.paper',
                transition: 'background-color 0.3s ease',
            }}
        >
            {/* Competitor Info */}
            {activeCompetitor && (
                <Box sx={{ mb: 3 }}>
                    <Typography
                        variant="h4"
                        fontWeight="bold"
                        color="primary.main"
                    >
                        #{activeCompetitor.starting_order}{' '}
                        {activeCompetitor.participant.name}
                    </Typography>
                    <Typography variant="h6" color="text.secondary">
                        {activeCompetitor.participant.hobby_horses.length > 0
                            ? activeCompetitor.participant.hobby_horses[0].name
                            : 'No horse assigned'}
                    </Typography>
                    {activeCompetitor.competition && (
                        <Box sx={{ mt: 1 }}>
                            <Chip
                                label={
                                    activeCompetitor.competition.height_class
                                }
                                color="primary"
                                sx={{ mr: 1 }}
                            />
                            <Chip
                                label={`Time limit: ${activeCompetitor.competition.time_limit}s`}
                                color="secondary"
                            />
                        </Box>
                    )}
                </Box>
            )}

            {/* Main Timer */}
            <Box sx={{ position: 'relative', mb: 3 }}>
                <Fade in={true}>
                    <Box>
                        <Typography
                            sx={{
                                fontSize: {
                                    xs: '200px',
                                    sm: '300px',
                                    md: '400px',
                                },
                                fontFamily: 'monospace',
                                fontWeight: 'bold',
                                color: timeColor,
                                lineHeight: 0.8,
                                textShadow: '2px 2px 4px rgba(0,0,0,0.1)',
                            }}
                        >
                            {minutes.toString().padStart(2, '0')}:
                            {seconds.toString().padStart(2, '0')}
                            <Typography
                                component="span"
                                sx={{
                                    color: millisecondsColor,
                                    fontSize: {
                                        xs: '120px',
                                        sm: '180px',
                                        md: '240px',
                                    },
                                    fontFamily: 'monospace',
                                    fontWeight: 'bold',
                                }}
                            >
                                .{milliseconds.toString().padStart(2, '0')}
                            </Typography>
                        </Typography>
                    </Box>
                </Fade>

                {/* Pause Indicator */}
                {isPaused && (
                    <Box
                        sx={{
                            position: 'absolute',
                            top: '50%',
                            left: '50%',
                            transform: 'translate(-50%, -50%)',
                            bgcolor: 'warning.main',
                            color: 'warning.contrastText',
                            p: 2,
                            borderRadius: 2,
                            fontSize: '2rem',
                            fontWeight: 'bold',
                            boxShadow: 3,
                            animation: 'pulse 1.5s infinite',
                            '@keyframes pulse': {
                                '0%': { opacity: 1 },
                                '50%': { opacity: 0.7 },
                                '100%': { opacity: 1 },
                            },
                        }}
                    >
                        ⏸️ PAUSED
                    </Box>
                )}

                {/* Overtime Warning */}
                {isOverTime && (
                    <Fade in={true}>
                        <Box
                            sx={{
                                position: 'absolute',
                                top: -20,
                                left: '50%',
                                transform: 'translateX(-50%)',
                                bgcolor: 'error.main',
                                color: 'white',
                                px: 2,
                                py: 1,
                                borderRadius: 2,
                                animation: 'pulse 1s infinite',
                                '@keyframes pulse': {
                                    '0%': { opacity: 1 },
                                    '50%': { opacity: 0.5 },
                                    '100%': { opacity: 1 },
                                },
                            }}
                        >
                            <Typography variant="h6" fontWeight="bold">
                                OVERTIME
                            </Typography>
                        </Box>
                    </Fade>
                )}
            </Box>

            {/* Status */}
            <Box
                sx={{
                    display: 'flex',
                    alignItems: 'center',
                    justifyContent: 'center',
                    gap: 2,
                }}
            >
                {getStatusIcon()}
                <Typography variant="h6" color="text.secondary">
                    {getStatusText()}
                </Typography>
            </Box>

            {/* Penalties */}
            {activeCompetitor && activeCompetitor.penalty_points > 0 && (
                <Box sx={{ mt: 2 }}>
                    <Chip
                        label={`Penalty Points: ${activeCompetitor.penalty_points}`}
                        color="error"
                        size="medium"
                    />
                </Box>
            )}

            {/* Pause Time Info */}
            {totalPauseTime > 0 && (
                <Box sx={{ mt: 2 }}>
                    <Chip
                        label={`Total Pause Time: ${(totalPauseTime / 1000).toFixed(2)}s`}
                        color="warning"
                        size="medium"
                    />
                </Box>
            )}
        </Paper>
    )
}

export default TimerDisplay
