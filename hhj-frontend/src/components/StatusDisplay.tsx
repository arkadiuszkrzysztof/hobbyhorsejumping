import React from 'react'
import { Box, Typography, Chip, Paper, Grid } from '@mui/material'
import {
    WifiOutlined,
    WifiOffOutlined,
    PersonOutlineOutlined,
    TimerOutlined,
} from '@mui/icons-material'
import { ReadyState } from 'react-use-websocket'
import { CompetitionStart } from '../types'

interface StatusDisplayProps {
    connectionStatus: ReadyState
    activeCompetitor: CompetitionStart | null
    lastMessage: MessageEvent | null
    messageHistory: MessageEvent[]
}

const StatusDisplay: React.FC<StatusDisplayProps> = ({
    connectionStatus,
    activeCompetitor,
    lastMessage,
    messageHistory,
}) => {
    const getConnectionIcon = () => {
        return connectionStatus === ReadyState.OPEN ? (
            <WifiOutlined color="success" />
        ) : (
            <WifiOffOutlined color="error" />
        )
    }

    const getConnectionStatus = () => {
        const statusMap = {
            [ReadyState.CONNECTING]: {
                label: 'Connecting',
                color: 'warning' as const,
            },
            [ReadyState.OPEN]: {
                label: 'Connected',
                color: 'success' as const,
            },
            [ReadyState.CLOSING]: {
                label: 'Disconnecting',
                color: 'warning' as const,
            },
            [ReadyState.CLOSED]: {
                label: 'Disconnected',
                color: 'error' as const,
            },
            [ReadyState.UNINSTANTIATED]: {
                label: 'Not Connected',
                color: 'default' as const,
            },
        }
        return statusMap[connectionStatus]
    }

    const recentMessages = messageHistory.slice(-5).reverse()

    return (
        <Paper elevation={1} sx={{ p: 2, mb: 2 }}>
            <Grid container spacing={2} alignItems="center">
                <Grid item xs={12} sm={6} md={3}>
                    <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
                        {getConnectionIcon()}
                        <Box>
                            <Typography
                                variant="caption"
                                color="text.secondary"
                            >
                                WebSocket Status
                            </Typography>
                            <br />
                            <Chip
                                label={getConnectionStatus().label}
                                color={getConnectionStatus().color}
                                size="small"
                            />
                        </Box>
                    </Box>
                </Grid>

                <Grid item xs={12} sm={6} md={4}>
                    <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
                        <PersonOutlineOutlined
                            color={activeCompetitor ? 'primary' : 'disabled'}
                        />
                        <Box>
                            <Typography
                                variant="caption"
                                color="text.secondary"
                            >
                                Active Competitor
                            </Typography>
                            <br />
                            <Typography variant="body2" fontWeight="bold">
                                {activeCompetitor
                                    ? `#${activeCompetitor.starting_order} ${activeCompetitor.participant.name}`
                                    : 'None selected'}
                            </Typography>
                        </Box>
                    </Box>
                </Grid>

                <Grid item xs={12} sm={12} md={5}>
                    <Box>
                        <Typography
                            variant="caption"
                            color="text.secondary"
                            sx={{
                                display: 'flex',
                                alignItems: 'center',
                                gap: 0.5,
                            }}
                        >
                            <TimerOutlined fontSize="small" />
                            Recent Messages
                        </Typography>
                        <Box sx={{ mt: 0.5, maxHeight: 60, overflow: 'auto' }}>
                            {recentMessages.length > 0 ? (
                                recentMessages.map((message, idx) => {
                                    try {
                                        const data = JSON.parse(message.data)
                                        return (
                                            <Typography
                                                key={idx}
                                                variant="caption"
                                                sx={{
                                                    display: 'block',
                                                    color:
                                                        data.time_mark ===
                                                        'START'
                                                            ? 'success.main'
                                                            : 'primary.main',
                                                }}
                                            >
                                                {data.time_mark} •{' '}
                                                {data.server_time
                                                    ? new Date(
                                                          data.server_time
                                                      ).toLocaleTimeString()
                                                    : 'No timestamp'}
                                            </Typography>
                                        )
                                    } catch {
                                        return (
                                            <Typography
                                                key={idx}
                                                variant="caption"
                                                color="text.disabled"
                                            >
                                                Invalid message
                                            </Typography>
                                        )
                                    }
                                })
                            ) : (
                                <Typography
                                    variant="caption"
                                    color="text.disabled"
                                >
                                    No messages received
                                </Typography>
                            )}
                        </Box>
                    </Box>
                </Grid>
            </Grid>
        </Paper>
    )
}

export default StatusDisplay
