import React, { useState } from 'react'
import {
    Box,
    Paper,
    Typography,
    Button,
    TextField,
    Alert,
    Divider,
    Grid,
    Card,
    CardContent,
    Chip,
} from '@mui/material'
import {
    PlayArrowOutlined,
    StopOutlined,
    SendOutlined,
    BugReportOutlined,
} from '@mui/icons-material'

const API_BASE_URL =
    process.env.REACT_APP_API_HTTP_URL || 'http://127.0.0.1:3003'

interface TestPageProps {
    onClose: () => void
}

const TestPage: React.FC<TestPageProps> = ({ onClose }) => {
    const [sensorTime, setSensorTime] = useState<string>('')
    const [customJson, setCustomJson] = useState<string>('')
    const [response, setResponse] = useState<string>('')
    const [loading, setLoading] = useState<boolean>(false)

    const getCurrentTimestamp = () => Date.now()

    const sendTimingSignal = async (
        timeMark: 'START' | 'FINISH',
        customTime?: number
    ) => {
        setLoading(true)
        setResponse('')

        try {
            const timestamp = customTime || getCurrentTimestamp()
            const payload = {
                time_mark: timeMark,
                sensor_time: timestamp.toString(),
            }

            const response = await fetch(`${API_BASE_URL}/timereadings/`, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify(payload),
            })

            const responseText = await response.text()

            if (response.ok) {
                setResponse(
                    `✅ ${timeMark} signal sent successfully!\n${responseText}`
                )
            } else {
                setResponse(`❌ Error: ${response.status}\n${responseText}`)
            }
        } catch (error) {
            setResponse(`❌ Network error: ${error}`)
        } finally {
            setLoading(false)
        }
    }

    const sendCustomJson = async () => {
        if (!customJson.trim()) return

        setLoading(true)
        setResponse('')

        try {
            const payload = JSON.parse(customJson)

            const response = await fetch(`${API_BASE_URL}/timereadings/`, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify(payload),
            })

            const responseText = await response.text()

            if (response.ok) {
                setResponse(
                    `✅ Custom payload sent successfully!\n${responseText}`
                )
            } else {
                setResponse(`❌ Error: ${response.status}\n${responseText}`)
            }
        } catch (error) {
            setResponse(`❌ Error: ${error}`)
        } finally {
            setLoading(false)
        }
    }

    const testApiEndpoints = async () => {
        setLoading(true)
        setResponse('Testing API endpoints...\n\n')

        const endpoints = [
            { name: 'Competitions', url: '/api/competitions/' },
            { name: 'Active Competitor', url: '/api/active-competitor/' },
            { name: 'Time Readings', url: '/timereadings/' },
        ]

        for (const endpoint of endpoints) {
            try {
                const response = await fetch(`${API_BASE_URL}${endpoint.url}`)
                const data = await response.text()

                setResponse(
                    (prev) =>
                        prev +
                        `${endpoint.name}: ${response.status} ${response.ok ? '✅' : '❌'}\n`
                )

                if (response.ok && endpoint.name === 'Competitions') {
                    const competitions = JSON.parse(data)
                    setResponse(
                        (prev) =>
                            prev +
                            `  Found ${competitions.length} competitions\n`
                    )
                }
            } catch (error) {
                setResponse(
                    (prev) => prev + `${endpoint.name}: Network Error ❌\n`
                )
            }
        }

        setResponse((prev) => prev + '\nAPI test completed!')
        setLoading(false)
    }

    const simulateCompleteRun = async () => {
        setResponse('🏁 Simulating complete timing sequence...\n\n')

        // Send START signal
        setResponse((prev) => prev + '⏱️ Sending START signal...\n')
        await sendTimingSignal('START')

        // Wait 2 seconds
        await new Promise((resolve) => setTimeout(resolve, 2000))

        // Send FINISH signal
        setResponse((prev) => prev + '🏁 Sending FINISH signal...\n')
        await sendTimingSignal('FINISH')

        setResponse((prev) => prev + '\n✅ Complete run simulation finished!')
    }

    const quickTests = [
        {
            name: 'START Signal',
            action: () => sendTimingSignal('START'),
            color: 'success' as const,
            icon: <PlayArrowOutlined />,
        },
        {
            name: 'FINISH Signal',
            action: () => sendTimingSignal('FINISH'),
            color: 'primary' as const,
            icon: <StopOutlined />,
        },
        {
            name: 'Complete Run',
            action: simulateCompleteRun,
            color: 'secondary' as const,
            icon: <PlayArrowOutlined />,
        },
        {
            name: 'START (5s ago)',
            action: () =>
                sendTimingSignal('START', getCurrentTimestamp() - 5000),
            color: 'info' as const,
            icon: <PlayArrowOutlined />,
        },
        {
            name: 'FINISH (2s ago)',
            action: () =>
                sendTimingSignal('FINISH', getCurrentTimestamp() - 2000),
            color: 'warning' as const,
            icon: <StopOutlined />,
        },
    ]

    return (
        <Box sx={{ p: 3, maxWidth: 1200, mx: 'auto' }}>
            <Paper elevation={3} sx={{ p: 3 }}>
                <Box
                    sx={{
                        display: 'flex',
                        justifyContent: 'space-between',
                        alignItems: 'center',
                        mb: 3,
                    }}
                >
                    <Typography
                        variant="h4"
                        sx={{ display: 'flex', alignItems: 'center', gap: 1 }}
                    >
                        <BugReportOutlined color="primary" />
                        Sensor Testing Interface
                    </Typography>
                    <Button variant="outlined" onClick={onClose}>
                        Back to Dashboard
                    </Button>
                </Box>

                <Alert severity="info" sx={{ mb: 3 }}>
                    This page simulates ESP32 sensor signals for testing the
                    timing system. Make sure you have selected an active
                    competitor in the main dashboard first!
                </Alert>

                <Grid container spacing={3}>
                    {/* Quick Test Buttons */}
                    <Grid item xs={12} md={6}>
                        <Card>
                            <CardContent>
                                <Typography variant="h6" gutterBottom>
                                    Quick Tests
                                </Typography>
                                <Typography
                                    variant="body2"
                                    color="text.secondary"
                                    gutterBottom
                                >
                                    Simulate common sensor scenarios
                                </Typography>

                                <Box
                                    sx={{
                                        display: 'flex',
                                        flexWrap: 'wrap',
                                        gap: 1,
                                        mb: 2,
                                    }}
                                >
                                    {quickTests.map((test, index) => (
                                        <Button
                                            key={index}
                                            variant="contained"
                                            color={test.color}
                                            startIcon={test.icon}
                                            onClick={test.action}
                                            disabled={loading}
                                            size="small"
                                        >
                                            {test.name}
                                        </Button>
                                    ))}
                                </Box>

                                <Divider sx={{ my: 2 }} />

                                <Typography variant="subtitle2" gutterBottom>
                                    Custom Timing
                                </Typography>
                                <TextField
                                    label="Custom Sensor Time (timestamp)"
                                    value={sensorTime}
                                    onChange={(e) =>
                                        setSensorTime(e.target.value)
                                    }
                                    placeholder={getCurrentTimestamp().toString()}
                                    fullWidth
                                    size="small"
                                    sx={{ mb: 1 }}
                                />
                                <Box sx={{ display: 'flex', gap: 1 }}>
                                    <Button
                                        variant="outlined"
                                        color="success"
                                        onClick={() =>
                                            sendTimingSignal(
                                                'START',
                                                parseInt(sensorTime) ||
                                                    getCurrentTimestamp()
                                            )
                                        }
                                        disabled={loading}
                                        size="small"
                                    >
                                        Custom START
                                    </Button>
                                    <Button
                                        variant="outlined"
                                        color="primary"
                                        onClick={() =>
                                            sendTimingSignal(
                                                'FINISH',
                                                parseInt(sensorTime) ||
                                                    getCurrentTimestamp()
                                            )
                                        }
                                        disabled={loading}
                                        size="small"
                                    >
                                        Custom FINISH
                                    </Button>
                                </Box>
                            </CardContent>
                        </Card>
                    </Grid>

                    {/* Custom JSON */}
                    <Grid item xs={12} md={6}>
                        <Card>
                            <CardContent>
                                <Typography variant="h6" gutterBottom>
                                    Custom JSON Payload
                                </Typography>
                                <Typography
                                    variant="body2"
                                    color="text.secondary"
                                    gutterBottom
                                >
                                    Send custom timing data to test edge cases
                                </Typography>

                                <TextField
                                    label="JSON Payload"
                                    value={customJson}
                                    onChange={(e) =>
                                        setCustomJson(e.target.value)
                                    }
                                    placeholder='{"time_mark": "START", "sensor_time": "1234567890"}'
                                    multiline
                                    rows={4}
                                    fullWidth
                                    sx={{ mb: 2 }}
                                />

                                <Box sx={{ display: 'flex', gap: 1, mb: 2 }}>
                                    <Button
                                        variant="contained"
                                        startIcon={<SendOutlined />}
                                        onClick={sendCustomJson}
                                        disabled={loading || !customJson.trim()}
                                    >
                                        Send Custom
                                    </Button>
                                    <Button
                                        variant="outlined"
                                        onClick={() =>
                                            setCustomJson(
                                                '{"time_mark": "START", "sensor_time": "' +
                                                    getCurrentTimestamp() +
                                                    '"}'
                                            )
                                        }
                                    >
                                        Template START
                                    </Button>
                                    <Button
                                        variant="outlined"
                                        onClick={() =>
                                            setCustomJson(
                                                '{"time_mark": "FINISH", "sensor_time": "' +
                                                    getCurrentTimestamp() +
                                                    '"}'
                                            )
                                        }
                                    >
                                        Template FINISH
                                    </Button>
                                </Box>

                                <Button
                                    variant="outlined"
                                    color="info"
                                    onClick={testApiEndpoints}
                                    disabled={loading}
                                    fullWidth
                                >
                                    Test All API Endpoints
                                </Button>
                            </CardContent>
                        </Card>
                    </Grid>

                    {/* Response Display */}
                    <Grid item xs={12}>
                        <Card>
                            <CardContent>
                                <Typography variant="h6" gutterBottom>
                                    Server Response
                                </Typography>

                                <Box sx={{ display: 'flex', gap: 1, mb: 2 }}>
                                    <Chip
                                        label={`API URL: ${API_BASE_URL}`}
                                        variant="outlined"
                                        size="small"
                                    />
                                    <Chip
                                        label={`Current Time: ${getCurrentTimestamp()}`}
                                        variant="outlined"
                                        size="small"
                                    />
                                </Box>

                                <TextField
                                    value={response}
                                    multiline
                                    rows={8}
                                    fullWidth
                                    variant="outlined"
                                    placeholder="Server responses will appear here..."
                                    InputProps={{
                                        readOnly: true,
                                        sx: {
                                            fontFamily: 'monospace',
                                            fontSize: '0.875rem',
                                        },
                                    }}
                                />
                            </CardContent>
                        </Card>
                    </Grid>
                </Grid>

                {loading && (
                    <Box
                        sx={{
                            display: 'flex',
                            justifyContent: 'center',
                            mt: 2,
                        }}
                    >
                        <Typography variant="body2" color="text.secondary">
                            Sending request...
                        </Typography>
                    </Box>
                )}
            </Paper>
        </Box>
    )
}

export default TestPage
