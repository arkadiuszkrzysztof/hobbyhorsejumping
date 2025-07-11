import React, { useState, useEffect } from 'react'
import {
    Box,
    Paper,
    Typography,
    Table,
    TableBody,
    TableCell,
    TableContainer,
    TableHead,
    TableRow,
    Chip,
    Button,
    Dialog,
    DialogTitle,
    DialogContent,
    DialogActions,
    FormControl,
    InputLabel,
    Select,
    MenuItem,
    Alert,
    IconButton,
    Tooltip,
} from '@mui/material'
import {
    PlayArrow,
    Stop,
    Edit,
    Check,
    Close,
    Warning,
} from '@mui/icons-material'
import { CompetitionStart } from '../types'
import { api } from '../api'

interface SignalEvent {
    id: string
    timestamp: number
    sensor_time: number
    server_time: number
    time_mark: 'START' | 'FINISH'
    competitor_id?: string
    is_selected: boolean
    created_at: string
}

interface SignalTrackingProps {
    activeCompetitor: CompetitionStart | null
    messageHistory: MessageEvent<any>[]
}

const SignalTracking: React.FC<SignalTrackingProps> = ({
    activeCompetitor,
    messageHistory,
}) => {
    const [signals, setSignals] = useState<SignalEvent[]>([])
    const [showReconciliation, setShowReconciliation] = useState(false)
    const [selectedStart, setSelectedStart] = useState<string>('')
    const [selectedFinish, setSelectedFinish] = useState<string>('')
    const [loading, setLoading] = useState(false)
    const [successMessage, setSuccessMessage] = useState<string>('')

    // Extract timing signals from message history
    useEffect(() => {
        if (!messageHistory.length) {
            setSignals([])
            return
        }

        const extractedSignals: SignalEvent[] = []

        messageHistory.forEach((message, index) => {
            try {
                const data = JSON.parse(message.data)
                if (
                    data.time_mark &&
                    (data.time_mark === 'START' || data.time_mark === 'FINISH')
                ) {
                    const sensorTime =
                        typeof data.sensor_time === 'string'
                            ? parseInt(data.sensor_time)
                            : data.sensor_time
                    const serverTime = data.server_time || message.timeStamp

                    extractedSignals.push({
                        id: `msg_${index}_${sensorTime}`,
                        timestamp: message.timeStamp,
                        sensor_time: sensorTime,
                        server_time: serverTime,
                        time_mark: data.time_mark,
                        competitor_id: activeCompetitor?.id,
                        is_selected: false,
                        created_at: new Date(message.timeStamp).toISOString(),
                    })
                }
            } catch (error) {
                console.error('Error parsing signal message:', error)
            }
        })

        // Sort by timestamp
        extractedSignals.sort((a, b) => a.timestamp - b.timestamp)

        // Auto-select first START and last FINISH for current competitor
        if (activeCompetitor) {
            const competitorSignals = extractedSignals.filter(
                (s) =>
                    s.competitor_id === activeCompetitor.id || !s.competitor_id
            )

            const startSignals = competitorSignals.filter(
                (s) => s.time_mark === 'START'
            )
            const finishSignals = competitorSignals.filter(
                (s) => s.time_mark === 'FINISH'
            )

            if (startSignals.length > 0 && !selectedStart) {
                setSelectedStart(startSignals[0].id)
                startSignals[0].is_selected = true
            }

            if (finishSignals.length > 0 && !selectedFinish) {
                setSelectedFinish(finishSignals[finishSignals.length - 1].id)
                finishSignals[finishSignals.length - 1].is_selected = true
            }
        }

        setSignals(extractedSignals)
    }, [messageHistory, activeCompetitor, selectedStart, selectedFinish])

    const formatTimestamp = (timestamp: number) => {
        return new Date(timestamp).toLocaleTimeString('en-US', {
            hour12: false,
            hour: '2-digit',
            minute: '2-digit',
            second: '2-digit',
            fractionalSecondDigits: 3,
        })
    }

    const getTimeDifference = (sensorTime: number, serverTime: number) => {
        const diff = Math.abs(sensorTime - serverTime)
        return diff > 1000 ? `±${diff}ms` : 'sync'
    }

    const handleReconciliation = async () => {
        if (!activeCompetitor || !selectedStart || !selectedFinish) {
            return
        }

        setLoading(true)
        try {
            const startSignal = signals.find((s) => s.id === selectedStart)
            const finishSignal = signals.find((s) => s.id === selectedFinish)

            if (!startSignal || !finishSignal) {
                throw new Error('Selected signals not found')
            }

            // Send reconciliation to backend
            const result = await api.reconcileTimingSignals({
                competitor_id: activeCompetitor.id,
                start_signal: {
                    sensor_time: startSignal.sensor_time,
                    server_time: startSignal.server_time,
                    timestamp: startSignal.timestamp,
                },
                finish_signal: {
                    sensor_time: finishSignal.sensor_time,
                    server_time: finishSignal.server_time,
                    timestamp: finishSignal.timestamp,
                },
            })

            setShowReconciliation(false)
            setSuccessMessage(
                `Reconciliation successful! Final time: ${result.reconciliation?.completion_time || 'calculated'}ms`
            )
            setTimeout(() => setSuccessMessage(''), 5000)
        } catch (error) {
            console.error('Reconciliation failed:', error)
        } finally {
            setLoading(false)
        }
    }

    const competitorSignals = signals.filter(
        (s) =>
            !activeCompetitor ||
            s.competitor_id === activeCompetitor.id ||
            !s.competitor_id
    )

    const startSignals = competitorSignals.filter(
        (s) => s.time_mark === 'START'
    )
    const finishSignals = competitorSignals.filter(
        (s) => s.time_mark === 'FINISH'
    )

    const hasMultipleSignals =
        startSignals.length > 1 || finishSignals.length > 1

    return (
        <Paper elevation={2} sx={{ p: 3, mb: 3 }}>
            <Box
                sx={{
                    display: 'flex',
                    justifyContent: 'space-between',
                    alignItems: 'center',
                    mb: 2,
                }}
            >
                <Typography
                    variant="h6"
                    sx={{ display: 'flex', alignItems: 'center', gap: 1 }}
                >
                    📡 Signal Tracking
                    {hasMultipleSignals && (
                        <Tooltip title="Multiple signals detected - reconciliation needed">
                            <Warning color="warning" />
                        </Tooltip>
                    )}
                </Typography>
                {hasMultipleSignals && (
                    <Button
                        variant="outlined"
                        startIcon={<Edit />}
                        onClick={() => setShowReconciliation(true)}
                        color="warning"
                    >
                        Reconcile Signals
                    </Button>
                )}
            </Box>

            {!activeCompetitor ? (
                <Alert severity="info">
                    Select a competitor to track timing signals
                </Alert>
            ) : (
                <>
                    {successMessage && (
                        <Alert severity="success" sx={{ mb: 2 }}>
                            {successMessage}
                        </Alert>
                    )}

                    <Typography
                        variant="body2"
                        color="text.secondary"
                        sx={{ mb: 2 }}
                    >
                        Tracking signals for: #{activeCompetitor.starting_order}{' '}
                        {activeCompetitor.participant.name}
                    </Typography>

                    <TableContainer>
                        <Table size="small">
                            <TableHead>
                                <TableRow>
                                    <TableCell>Time</TableCell>
                                    <TableCell>Type</TableCell>
                                    <TableCell>Sensor Time</TableCell>
                                    <TableCell>Server Sync</TableCell>
                                    <TableCell>Status</TableCell>
                                </TableRow>
                            </TableHead>
                            <TableBody>
                                {competitorSignals.length === 0 ? (
                                    <TableRow>
                                        <TableCell colSpan={5} align="center">
                                            <Typography color="text.secondary">
                                                No signals recorded yet
                                            </Typography>
                                        </TableCell>
                                    </TableRow>
                                ) : (
                                    competitorSignals.map((signal) => (
                                        <TableRow
                                            key={signal.id}
                                            sx={{
                                                bgcolor: signal.is_selected
                                                    ? 'success.light'
                                                    : 'transparent',
                                                '&:hover': {
                                                    bgcolor: 'action.hover',
                                                },
                                            }}
                                        >
                                            <TableCell>
                                                {formatTimestamp(
                                                    signal.timestamp
                                                )}
                                            </TableCell>
                                            <TableCell>
                                                <Chip
                                                    icon={
                                                        signal.time_mark ===
                                                        'START' ? (
                                                            <PlayArrow />
                                                        ) : (
                                                            <Stop />
                                                        )
                                                    }
                                                    label={signal.time_mark}
                                                    color={
                                                        signal.time_mark ===
                                                        'START'
                                                            ? 'success'
                                                            : 'error'
                                                    }
                                                    size="small"
                                                />
                                            </TableCell>
                                            <TableCell>
                                                <Typography
                                                    variant="body2"
                                                    fontFamily="monospace"
                                                >
                                                    {signal.sensor_time}ms
                                                </Typography>
                                            </TableCell>
                                            <TableCell>
                                                <Typography
                                                    variant="body2"
                                                    color={
                                                        getTimeDifference(
                                                            signal.sensor_time,
                                                            signal.server_time
                                                        ) === 'sync'
                                                            ? 'success.main'
                                                            : 'warning.main'
                                                    }
                                                >
                                                    {getTimeDifference(
                                                        signal.sensor_time,
                                                        signal.server_time
                                                    )}
                                                </Typography>
                                            </TableCell>
                                            <TableCell>
                                                {signal.is_selected && (
                                                    <Chip
                                                        icon={<Check />}
                                                        label="Selected"
                                                        color="success"
                                                        size="small"
                                                    />
                                                )}
                                            </TableCell>
                                        </TableRow>
                                    ))
                                )}
                            </TableBody>
                        </Table>
                    </TableContainer>

                    {startSignals.length > 1 && (
                        <Alert severity="warning" sx={{ mt: 2 }}>
                            Multiple START signals detected (
                            {startSignals.length}). Current selection:{' '}
                            {formatTimestamp(
                                startSignals.find((s) => s.id === selectedStart)
                                    ?.timestamp || 0
                            )}
                        </Alert>
                    )}

                    {finishSignals.length > 1 && (
                        <Alert severity="warning" sx={{ mt: 1 }}>
                            Multiple FINISH signals detected (
                            {finishSignals.length}). Current selection:{' '}
                            {formatTimestamp(
                                finishSignals.find(
                                    (s) => s.id === selectedFinish
                                )?.timestamp || 0
                            )}
                        </Alert>
                    )}
                </>
            )}

            {/* Reconciliation Dialog */}
            <Dialog
                open={showReconciliation}
                onClose={() => setShowReconciliation(false)}
                maxWidth="md"
                fullWidth
            >
                <DialogTitle>
                    Signal Reconciliation
                    <IconButton
                        onClick={() => setShowReconciliation(false)}
                        sx={{ position: 'absolute', right: 8, top: 8 }}
                    >
                        <Close />
                    </IconButton>
                </DialogTitle>
                <DialogContent>
                    <Typography
                        variant="body2"
                        color="text.secondary"
                        sx={{ mb: 3 }}
                    >
                        Multiple timing signals detected. Select the correct
                        START and FINISH signals for the run.
                    </Typography>

                    <Box sx={{ display: 'flex', gap: 3 }}>
                        <FormControl fullWidth>
                            <InputLabel>START Signal</InputLabel>
                            <Select
                                value={selectedStart}
                                onChange={(e) =>
                                    setSelectedStart(e.target.value)
                                }
                                label="START Signal"
                            >
                                {startSignals.map((signal) => (
                                    <MenuItem key={signal.id} value={signal.id}>
                                        {formatTimestamp(signal.timestamp)} -{' '}
                                        {signal.sensor_time}ms
                                    </MenuItem>
                                ))}
                            </Select>
                        </FormControl>

                        <FormControl fullWidth>
                            <InputLabel>FINISH Signal</InputLabel>
                            <Select
                                value={selectedFinish}
                                onChange={(e) =>
                                    setSelectedFinish(e.target.value)
                                }
                                label="FINISH Signal"
                            >
                                {finishSignals.map((signal) => (
                                    <MenuItem key={signal.id} value={signal.id}>
                                        {formatTimestamp(signal.timestamp)} -{' '}
                                        {signal.sensor_time}ms
                                    </MenuItem>
                                ))}
                            </Select>
                        </FormControl>
                    </Box>

                    {selectedStart && selectedFinish && (
                        <Alert severity="info" sx={{ mt: 2 }}>
                            <Typography variant="body2">
                                Selected time:{' '}
                                {signals.find((s) => s.id === selectedFinish)
                                    ?.sensor_time! -
                                    signals.find((s) => s.id === selectedStart)
                                        ?.sensor_time!}
                                ms
                            </Typography>
                        </Alert>
                    )}
                </DialogContent>
                <DialogActions>
                    <Button onClick={() => setShowReconciliation(false)}>
                        Cancel
                    </Button>
                    <Button
                        onClick={handleReconciliation}
                        variant="contained"
                        disabled={!selectedStart || !selectedFinish || loading}
                    >
                        {loading ? 'Applying...' : 'Apply Reconciliation'}
                    </Button>
                </DialogActions>
            </Dialog>
        </Paper>
    )
}

export default SignalTracking
