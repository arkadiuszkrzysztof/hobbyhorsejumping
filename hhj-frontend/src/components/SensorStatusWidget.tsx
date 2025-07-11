import React, { useState, useEffect } from 'react'
import {
    Box,
    Typography,
    Chip,
    Paper,
    Grid,
    Alert,
    IconButton,
    Collapse,
} from '@mui/material'
import {
    Sensors,
    WifiOutlined,
    WifiOffOutlined,
    ExpandMore,
    ExpandLess,
    PlayArrow,
    Stop,
    Router,
    Devices,
} from '@mui/icons-material'
import { ReadyState } from 'react-use-websocket'
import { api } from '../api'

interface SensorData {
    sensor_id: string
    sensor_type: 'START_SENSOR' | 'FINISH_SENSOR'
    is_online: boolean
    last_alive_signal: string | null
    seconds_since_last_signal: number | null
}

interface SensorStatusResponse {
    sensors: SensorData[]
    total_sensors: number
    online_sensors: number
    timestamp: string
}

interface ConnectedClient {
    id: string
    type: 'dashboard' | 'public_display'
    connected_at: string
    last_activity: string
}

interface SensorStatusWidgetProps {
    connectionStatus: ReadyState
    messageHistory: MessageEvent[]
}

const SensorStatusWidget: React.FC<SensorStatusWidgetProps> = ({
    connectionStatus,
    messageHistory,
}) => {
    const [sensorStatus, setSensorStatus] =
        useState<SensorStatusResponse | null>(null)
    const [connectedClients, setConnectedClients] = useState<ConnectedClient[]>(
        []
    )
    const [expanded, setExpanded] = useState(false)
    const [loading, setLoading] = useState(false)
    const [error, setError] = useState<string | null>(null)

    // Fetch sensor status
    const fetchSensorStatus = async () => {
        try {
            setLoading(true)
            const data = await api.getSensorStatus()
            setSensorStatus(data)
            setError(null)
        } catch (err) {
            setError(
                err instanceof Error
                    ? err.message
                    : 'Failed to fetch sensor status'
            )
        } finally {
            setLoading(false)
        }
    }

    // Simulate connected clients data (in a real implementation, this would come from the backend)
    const mockConnectedClients = (): ConnectedClient[] => {
        const clients: ConnectedClient[] = []

        // Add current dashboard
        if (connectionStatus === ReadyState.OPEN) {
            clients.push({
                id: 'dashboard-main',
                type: 'dashboard',
                connected_at: new Date().toISOString(),
                last_activity: new Date().toISOString(),
            })
        }

        // Simulate additional connected displays based on message activity
        const recentMessages = messageHistory.slice(-10)
        if (recentMessages.length > 0) {
            clients.push({
                id: 'public-display-1',
                type: 'public_display',
                connected_at: new Date(Date.now() - 300000).toISOString(), // 5 minutes ago
                last_activity: new Date().toISOString(),
            })
        }

        return clients
    }

    useEffect(() => {
        // Initial fetch
        fetchSensorStatus()
        setConnectedClients(mockConnectedClients())

        // Set up polling for sensor status every 15 seconds
        const interval = setInterval(() => {
            fetchSensorStatus()
            setConnectedClients(mockConnectedClients())
        }, 15000)

        return () => clearInterval(interval)
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [connectionStatus, messageHistory])

    const getSensorIcon = (sensorType: string, isOnline: boolean) => {
        const IconComponent = sensorType === 'START_SENSOR' ? PlayArrow : Stop
        return (
            <IconComponent
                color={isOnline ? 'success' : 'error'}
                fontSize="small"
            />
        )
    }

    const getConnectionStatusColor = () => {
        switch (connectionStatus) {
            case ReadyState.OPEN:
                return 'success'
            case ReadyState.CONNECTING:
                return 'warning'
            default:
                return 'error'
        }
    }

    const getConnectionStatusText = () => {
        switch (connectionStatus) {
            case ReadyState.OPEN:
                return 'Connected'
            case ReadyState.CONNECTING:
                return 'Connecting'
            case ReadyState.CLOSING:
                return 'Disconnecting'
            case ReadyState.CLOSED:
                return 'Disconnected'
            default:
                return 'Unknown'
        }
    }

    const formatTimeAgo = (timestamp: string | null) => {
        if (!timestamp) return 'Never'
        const seconds = Math.floor(
            (Date.now() - new Date(timestamp).getTime()) / 1000
        )
        if (seconds < 60) return `${seconds}s ago`
        if (seconds < 3600) return `${Math.floor(seconds / 60)}m ago`
        return `${Math.floor(seconds / 3600)}h ago`
    }

    return (
        <Paper elevation={2} sx={{ p: 2, mb: 3 }}>
            <Box
                sx={{
                    display: 'flex',
                    justifyContent: 'space-between',
                    alignItems: 'center',
                }}
            >
                <Typography
                    variant="h6"
                    sx={{ display: 'flex', alignItems: 'center', gap: 1 }}
                >
                    <Sensors />
                    System Status
                    {sensorStatus && (
                        <Chip
                            size="small"
                            label={`${sensorStatus.online_sensors}/${sensorStatus.total_sensors} Sensors Online`}
                            color={
                                sensorStatus.online_sensors ===
                                sensorStatus.total_sensors
                                    ? 'success'
                                    : 'warning'
                            }
                        />
                    )}
                </Typography>
                <IconButton
                    size="small"
                    onClick={() => setExpanded(!expanded)}
                    aria-label="expand"
                >
                    {expanded ? <ExpandLess /> : <ExpandMore />}
                </IconButton>
            </Box>

            {/* Quick Status Row */}
            <Grid container spacing={2} sx={{ mt: 1 }}>
                <Grid item xs={12} sm={4}>
                    <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
                        <Router fontSize="small" />
                        <Typography variant="body2">WebSocket:</Typography>
                        <Chip
                            size="small"
                            label={getConnectionStatusText()}
                            color={getConnectionStatusColor()}
                            icon={
                                connectionStatus === ReadyState.OPEN ? (
                                    <WifiOutlined />
                                ) : (
                                    <WifiOffOutlined />
                                )
                            }
                        />
                    </Box>
                </Grid>
                <Grid item xs={12} sm={4}>
                    <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
                        <Devices fontSize="small" />
                        <Typography variant="body2">Displays:</Typography>
                        <Chip
                            size="small"
                            label={`${connectedClients.length} Connected`}
                            color={
                                connectedClients.length > 0
                                    ? 'success'
                                    : 'warning'
                            }
                        />
                    </Box>
                </Grid>
                <Grid item xs={12} sm={4}>
                    <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
                        <Sensors fontSize="small" />
                        <Typography variant="body2">
                            {loading ? 'Checking...' : 'Sensors OK'}
                        </Typography>
                    </Box>
                </Grid>
            </Grid>

            {/* Detailed Status - Collapsible */}
            <Collapse in={expanded}>
                <Box sx={{ mt: 3 }}>
                    {error && (
                        <Alert severity="error" sx={{ mb: 2 }}>
                            {error}
                        </Alert>
                    )}

                    {/* Sensor Details */}
                    {sensorStatus && (
                        <Box sx={{ mb: 3 }}>
                            <Typography variant="subtitle2" gutterBottom>
                                📡 Sensor Status
                            </Typography>
                            <Grid container spacing={1}>
                                {sensorStatus.sensors.map((sensor) => (
                                    <Grid
                                        item
                                        xs={12}
                                        sm={6}
                                        key={sensor.sensor_id}
                                    >
                                        <Box
                                            sx={{
                                                p: 1.5,
                                                border: 1,
                                                borderColor: sensor.is_online
                                                    ? 'success.main'
                                                    : 'error.main',
                                                borderRadius: 1,
                                                bgcolor: sensor.is_online
                                                    ? 'success.light'
                                                    : 'error.light',
                                                opacity: sensor.is_online
                                                    ? 1
                                                    : 0.7,
                                            }}
                                        >
                                            <Box
                                                sx={{
                                                    display: 'flex',
                                                    alignItems: 'center',
                                                    gap: 1,
                                                    mb: 0.5,
                                                }}
                                            >
                                                {getSensorIcon(
                                                    sensor.sensor_type,
                                                    sensor.is_online
                                                )}
                                                <Typography
                                                    variant="body2"
                                                    fontWeight="bold"
                                                >
                                                    {sensor.sensor_id}
                                                </Typography>
                                                <Chip
                                                    size="small"
                                                    label={
                                                        sensor.is_online
                                                            ? 'Online'
                                                            : 'Offline'
                                                    }
                                                    color={
                                                        sensor.is_online
                                                            ? 'success'
                                                            : 'error'
                                                    }
                                                />
                                            </Box>
                                            <Typography
                                                variant="caption"
                                                color="text.secondary"
                                            >
                                                Last signal:{' '}
                                                {formatTimeAgo(
                                                    sensor.last_alive_signal
                                                )}
                                            </Typography>
                                        </Box>
                                    </Grid>
                                ))}
                                {sensorStatus.sensors.length === 0 && (
                                    <Grid item xs={12}>
                                        <Alert severity="info">
                                            No sensors registered yet. Start
                                            sensors to see their status here.
                                        </Alert>
                                    </Grid>
                                )}
                            </Grid>
                        </Box>
                    )}

                    {/* Connected Displays */}
                    <Box>
                        <Typography variant="subtitle2" gutterBottom>
                            🖥️ Connected Displays
                        </Typography>
                        <Grid container spacing={1}>
                            {connectedClients.map((client) => (
                                <Grid item xs={12} sm={6} key={client.id}>
                                    <Box
                                        sx={{
                                            p: 1.5,
                                            border: 1,
                                            borderColor: 'primary.main',
                                            borderRadius: 1,
                                            bgcolor: 'primary.light',
                                            opacity: 0.9,
                                        }}
                                    >
                                        <Box
                                            sx={{
                                                display: 'flex',
                                                alignItems: 'center',
                                                gap: 1,
                                                mb: 0.5,
                                            }}
                                        >
                                            <Devices fontSize="small" />
                                            <Typography
                                                variant="body2"
                                                fontWeight="bold"
                                            >
                                                {client.type === 'dashboard'
                                                    ? 'Control Dashboard'
                                                    : 'Public Display'}
                                            </Typography>
                                            <Chip
                                                size="small"
                                                label="Connected"
                                                color="primary"
                                            />
                                        </Box>
                                        <Typography
                                            variant="caption"
                                            color="text.secondary"
                                        >
                                            Connected:{' '}
                                            {formatTimeAgo(client.connected_at)}
                                        </Typography>
                                    </Box>
                                </Grid>
                            ))}
                        </Grid>
                    </Box>
                </Box>
            </Collapse>
        </Paper>
    )
}

export default SensorStatusWidget
