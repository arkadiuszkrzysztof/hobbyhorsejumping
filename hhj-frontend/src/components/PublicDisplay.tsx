import React, { useState, useEffect } from 'react'
import useWebSocket, { ReadyState } from 'react-use-websocket'
import {
    Box,
    Typography,
    Paper,
    Table,
    TableBody,
    TableCell,
    TableContainer,
    TableHead,
    TableRow,
    Chip,
    Grid,
    Fade,
    AppBar,
    Toolbar,
    Container,
    Alert,
    LinearProgress,
    Card,
    CardContent,
} from '@mui/material'
import {
    EmojiEvents,
    Timer,
    Person,
    Pets,
    SignalWifiOff,
    Wifi,
} from '@mui/icons-material'
import { Competition, CompetitionStart } from '../types'

const API_BASE_URL =
    process.env.REACT_APP_API_HTTP_URL || 'http://127.0.0.1:3003'
const WS_URL = process.env.REACT_APP_API_WEBSOCKET_URL || 'ws://127.0.0.1:3003'

interface PublicDisplayData {
    active_competitor: CompetitionStart | null
    competitions: {
        competition: Competition
        rankings: CompetitionStart[]
        all_competitors: CompetitionStart[]
    }[]
    timestamp: string
}

const PublicDisplay: React.FC = () => {
    const [displayData, setDisplayData] = useState<PublicDisplayData | null>(
        null
    )
    const [currentTime, setCurrentTime] = useState<Date>(new Date())
    const [error, setError] = useState<string | null>(null)
    const [loading, setLoading] = useState(true)

    // WebSocket connection for real-time updates
    const { lastMessage, readyState } = useWebSocket(`${WS_URL}/ws/timings/`)

    // Update current time every second
    useEffect(() => {
        const timer = setInterval(() => {
            setCurrentTime(new Date())
        }, 1000)
        return () => clearInterval(timer)
    }, [])

    // Fetch initial display data
    useEffect(() => {
        const fetchDisplayData = async () => {
            try {
                setLoading(true)
                const response = await fetch(
                    `${API_BASE_URL}/api/public-display/`
                )
                if (!response.ok) {
                    throw new Error(
                        `HTTP ${response.status}: ${response.statusText}`
                    )
                }
                const data = await response.json()
                setDisplayData(data)
                setError(null)
            } catch (err) {
                setError(
                    err instanceof Error
                        ? err.message
                        : 'Failed to load display data'
                )
                console.error('Failed to fetch display data:', err)
            } finally {
                setLoading(false)
            }
        }

        fetchDisplayData()
        // Refresh every 30 seconds as backup
        const interval = setInterval(fetchDisplayData, 30000)
        return () => clearInterval(interval)
    }, [])

    // Handle WebSocket messages for real-time updates
    useEffect(() => {
        if (lastMessage) {
            try {
                const message = JSON.parse(lastMessage.data)

                if (
                    message.type === 'state_update' ||
                    message.type === 'competition_update' ||
                    message.type === 'competitor_reorder'
                ) {
                    // Refresh display data when state changes
                    fetch(`${API_BASE_URL}/api/public-display/`)
                        .then((response) => response.json())
                        .then((data) => setDisplayData(data))
                        .catch(console.error)
                }
            } catch (err) {
                console.error('Error parsing WebSocket message:', err)
            }
        }
    }, [lastMessage])

    const getConnectionStatusColor = ():
        | 'default'
        | 'success'
        | 'warning'
        | 'error' => {
        switch (readyState) {
            case ReadyState.CONNECTING:
                return 'warning'
            case ReadyState.OPEN:
                return 'success'
            case ReadyState.CLOSING:
            case ReadyState.CLOSED:
                return 'error'
            default:
                return 'default'
        }
    }

    const getConnectionStatusText = () => {
        switch (readyState) {
            case ReadyState.CONNECTING:
                return 'Connecting...'
            case ReadyState.OPEN:
                return 'Live'
            case ReadyState.CLOSING:
                return 'Disconnecting...'
            case ReadyState.CLOSED:
                return 'Disconnected'
            default:
                return 'Unknown'
        }
    }

    const formatTime = (milliseconds: number | null | string | undefined) => {
        if (!milliseconds) return '--:--:--'
        const ms =
            typeof milliseconds === 'string'
                ? parseInt(milliseconds)
                : milliseconds
        const totalSeconds = Math.floor(ms / 1000)
        const minutes = Math.floor(totalSeconds / 60)
        const seconds = totalSeconds % 60
        const centiseconds = Math.floor((ms % 1000) / 10)
        return `${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}.${centiseconds.toString().padStart(2, '0')}`
    }

    const getRankColor = (index: number) => {
        switch (index) {
            case 0:
                return '#FFD700' // Gold
            case 1:
                return '#C0C0C0' // Silver
            case 2:
                return '#CD7F32' // Bronze
            default:
                return 'transparent'
        }
    }

    if (loading) {
        return (
            <Box sx={{ p: 4 }}>
                <LinearProgress />
                <Typography variant="h6" sx={{ mt: 2, textAlign: 'center' }}>
                    Loading Competition Data...
                </Typography>
            </Box>
        )
    }

    if (error) {
        return (
            <Container maxWidth="md" sx={{ mt: 4 }}>
                <Alert severity="error">
                    <Typography variant="h6">Connection Error</Typography>
                    <Typography>{error}</Typography>
                </Alert>
            </Container>
        )
    }

    return (
        <Box sx={{ minHeight: '100vh', bgcolor: 'background.default' }}>
            {/* Header */}
            <AppBar position="static" color="primary">
                <Toolbar>
                    <EmojiEvents sx={{ mr: 2, fontSize: 40 }} />
                    <Typography
                        variant="h4"
                        component="div"
                        sx={{ flexGrow: 1, fontWeight: 'bold' }}
                    >
                        🐴 Hobby Horse Jumping Championship - Live Results
                    </Typography>
                    <Box sx={{ textAlign: 'right' }}>
                        <Typography variant="h6">
                            {currentTime.toLocaleTimeString()}
                        </Typography>
                        <Chip
                            icon={
                                readyState === ReadyState.OPEN ? (
                                    <Wifi />
                                ) : (
                                    <SignalWifiOff />
                                )
                            }
                            label={getConnectionStatusText()}
                            color={getConnectionStatusColor()}
                            size="small"
                        />
                    </Box>
                </Toolbar>
            </AppBar>

            <Container maxWidth="xl" sx={{ py: 3 }}>
                {/* Active Competitor Display */}
                {displayData?.active_competitor && (
                    <Fade in={true}>
                        <Paper
                            elevation={6}
                            sx={{
                                p: 4,
                                mb: 3,
                                bgcolor: 'warning.light',
                                border: '3px solid',
                                borderColor: 'warning.main',
                            }}
                        >
                            <Grid container spacing={3} alignItems="center">
                                <Grid item xs={12} md={8}>
                                    <Typography
                                        variant="h4"
                                        fontWeight="bold"
                                        color="primary.main"
                                        sx={{ mb: 1 }}
                                    >
                                        🎯 NOW COMPETING
                                    </Typography>
                                    <Typography
                                        variant="h2"
                                        fontWeight="bold"
                                        sx={{ mb: 1 }}
                                    >
                                        #
                                        {
                                            displayData.active_competitor
                                                .starting_order
                                        }{' '}
                                        {
                                            displayData.active_competitor
                                                .participant.name
                                        }
                                    </Typography>
                                    <Typography
                                        variant="h5"
                                        color="text.secondary"
                                        sx={{
                                            display: 'flex',
                                            alignItems: 'center',
                                            gap: 1,
                                        }}
                                    >
                                        <Pets />
                                        {displayData.active_competitor
                                            .participant.hobby_horses.length > 0
                                            ? displayData.active_competitor
                                                  .participant.hobby_horses[0]
                                                  .name
                                            : 'No horse assigned'}
                                    </Typography>
                                    {displayData.active_competitor
                                        .competition && (
                                        <Box sx={{ mt: 2 }}>
                                            <Chip
                                                label={
                                                    displayData
                                                        .active_competitor
                                                        .competition
                                                        .height_class
                                                }
                                                color="primary"
                                                sx={{
                                                    mr: 1,
                                                    fontSize: '1.1rem',
                                                }}
                                            />
                                            <Chip
                                                label={`Time Limit: ${displayData.active_competitor.competition.time_limit}s`}
                                                color="secondary"
                                                sx={{ fontSize: '1.1rem' }}
                                            />
                                        </Box>
                                    )}
                                </Grid>
                                <Grid item xs={12} md={4}>
                                    <Card
                                        elevation={3}
                                        sx={{ bgcolor: 'background.paper' }}
                                    >
                                        <CardContent
                                            sx={{ textAlign: 'center' }}
                                        >
                                            <Timer
                                                sx={{
                                                    fontSize: 60,
                                                    color: 'primary.main',
                                                    mb: 1,
                                                }}
                                            />
                                            <Typography
                                                variant="h6"
                                                color="text.secondary"
                                            >
                                                Status
                                            </Typography>
                                            <Typography
                                                variant="h5"
                                                fontWeight="bold"
                                                color={
                                                    displayData
                                                        .active_competitor
                                                        .has_finished
                                                        ? 'success.main'
                                                        : displayData
                                                                .active_competitor
                                                                .has_started
                                                          ? 'warning.main'
                                                          : 'text.primary'
                                                }
                                            >
                                                {displayData.active_competitor
                                                    .has_finished
                                                    ? '✅ Finished'
                                                    : displayData
                                                            .active_competitor
                                                            .has_started
                                                      ? '⏱️ In Progress'
                                                      : '🎯 Ready'}
                                            </Typography>
                                            {displayData.active_competitor
                                                .completion_time && (
                                                <Typography
                                                    variant="h4"
                                                    fontWeight="bold"
                                                    color="success.main"
                                                    sx={{ mt: 1 }}
                                                >
                                                    {formatTime(
                                                        displayData
                                                            .active_competitor
                                                            .completion_time
                                                    )}
                                                </Typography>
                                            )}
                                        </CardContent>
                                    </Card>
                                </Grid>
                            </Grid>
                        </Paper>
                    </Fade>
                )}

                {/* Competition Results */}
                {displayData?.competitions.map((competitionData) => (
                    <Paper
                        key={competitionData.competition.id}
                        elevation={3}
                        sx={{ mb: 3 }}
                    >
                        <Box
                            sx={{
                                p: 3,
                                bgcolor: 'primary.main',
                                color: 'white',
                            }}
                        >
                            <Typography variant="h4" fontWeight="bold">
                                🏆 {competitionData.competition.height_class}
                            </Typography>
                            <Typography variant="body1" sx={{ opacity: 0.9 }}>
                                {competitionData.competition.description ||
                                    'Competition Results'}
                            </Typography>
                        </Box>

                        {competitionData.rankings.length > 0 ? (
                            <TableContainer>
                                <Table>
                                    <TableHead>
                                        <TableRow sx={{ bgcolor: 'grey.100' }}>
                                            <TableCell
                                                sx={{
                                                    fontWeight: 'bold',
                                                    fontSize: '1.1rem',
                                                }}
                                            >
                                                Rank
                                            </TableCell>
                                            <TableCell
                                                sx={{
                                                    fontWeight: 'bold',
                                                    fontSize: '1.1rem',
                                                }}
                                            >
                                                Competitor
                                            </TableCell>
                                            <TableCell
                                                sx={{
                                                    fontWeight: 'bold',
                                                    fontSize: '1.1rem',
                                                }}
                                            >
                                                Horse
                                            </TableCell>
                                            <TableCell
                                                sx={{
                                                    fontWeight: 'bold',
                                                    fontSize: '1.1rem',
                                                }}
                                            >
                                                Time
                                            </TableCell>
                                            <TableCell
                                                sx={{
                                                    fontWeight: 'bold',
                                                    fontSize: '1.1rem',
                                                }}
                                            >
                                                Penalties
                                            </TableCell>
                                            <TableCell
                                                sx={{
                                                    fontWeight: 'bold',
                                                    fontSize: '1.1rem',
                                                }}
                                            >
                                                Total Score
                                            </TableCell>
                                        </TableRow>
                                    </TableHead>
                                    <TableBody>
                                        {competitionData.rankings.map(
                                            (competitor, rankIndex) => (
                                                <TableRow
                                                    key={competitor.id}
                                                    sx={{
                                                        bgcolor:
                                                            getRankColor(
                                                                rankIndex
                                                            ),
                                                        '&:hover': {
                                                            bgcolor:
                                                                'action.hover',
                                                        },
                                                    }}
                                                >
                                                    <TableCell>
                                                        <Typography
                                                            variant="h5"
                                                            fontWeight="bold"
                                                            sx={{
                                                                display: 'flex',
                                                                alignItems:
                                                                    'center',
                                                                gap: 1,
                                                            }}
                                                        >
                                                            {rankIndex < 3 &&
                                                                [
                                                                    '🥇',
                                                                    '🥈',
                                                                    '🥉',
                                                                ][rankIndex]}
                                                            {rankIndex + 1}
                                                        </Typography>
                                                    </TableCell>
                                                    <TableCell>
                                                        <Typography
                                                            variant="h6"
                                                            fontWeight="bold"
                                                        >
                                                            #
                                                            {
                                                                competitor.starting_order
                                                            }{' '}
                                                            {
                                                                competitor
                                                                    .participant
                                                                    .name
                                                            }
                                                        </Typography>
                                                    </TableCell>
                                                    <TableCell>
                                                        <Typography variant="body1">
                                                            {competitor
                                                                .participant
                                                                .hobby_horses
                                                                .length > 0
                                                                ? competitor
                                                                      .participant
                                                                      .hobby_horses[0]
                                                                      .name
                                                                : 'No horse assigned'}
                                                        </Typography>
                                                    </TableCell>
                                                    <TableCell>
                                                        <Typography
                                                            variant="h6"
                                                            fontFamily="monospace"
                                                        >
                                                            {formatTime(
                                                                competitor.completion_time
                                                            )}
                                                        </Typography>
                                                    </TableCell>
                                                    <TableCell>
                                                        <Chip
                                                            label={
                                                                competitor.penalty_points ||
                                                                0
                                                            }
                                                            color={
                                                                competitor.penalty_points >
                                                                0
                                                                    ? 'error'
                                                                    : 'success'
                                                            }
                                                            size="medium"
                                                        />
                                                    </TableCell>
                                                    <TableCell>
                                                        <Typography
                                                            variant="h6"
                                                            fontWeight="bold"
                                                        >
                                                            {competitor.completion_time
                                                                ? (
                                                                      (typeof competitor.completion_time ===
                                                                      'string'
                                                                          ? parseInt(
                                                                                competitor.completion_time
                                                                            )
                                                                          : competitor.completion_time) /
                                                                          1000 +
                                                                      (competitor.penalty_points ||
                                                                          0)
                                                                  ).toFixed(2) +
                                                                  's'
                                                                : 'N/A'}
                                                        </Typography>
                                                    </TableCell>
                                                </TableRow>
                                            )
                                        )}
                                    </TableBody>
                                </Table>
                            </TableContainer>
                        ) : (
                            <Box sx={{ p: 4, textAlign: 'center' }}>
                                <Person
                                    sx={{
                                        fontSize: 80,
                                        color: 'grey.400',
                                        mb: 2,
                                    }}
                                />
                                <Typography variant="h6" color="text.secondary">
                                    No completed runs yet
                                </Typography>
                                <Typography
                                    variant="body2"
                                    color="text.secondary"
                                >
                                    Results will appear here as competitors
                                    finish
                                </Typography>
                            </Box>
                        )}
                    </Paper>
                ))}

                {(!displayData || displayData.competitions.length === 0) && (
                    <Paper elevation={3} sx={{ p: 6, textAlign: 'center' }}>
                        <EmojiEvents
                            sx={{ fontSize: 120, color: 'grey.400', mb: 2 }}
                        />
                        <Typography
                            variant="h5"
                            color="text.secondary"
                            gutterBottom
                        >
                            No Active Competitions
                        </Typography>
                        <Typography variant="body1" color="text.secondary">
                            Competition results will be displayed here when
                            available
                        </Typography>
                    </Paper>
                )}
            </Container>
        </Box>
    )
}

export default PublicDisplay
