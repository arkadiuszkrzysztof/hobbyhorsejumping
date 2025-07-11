import React, { useState, useEffect } from 'react'
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
    LinearProgress,
    List,
    ListItem,
    ListItemText,
    ListItemIcon,
} from '@mui/material'

const API_BASE_URL =
    process.env.REACT_APP_API_HTTP_URL || 'http://127.0.0.1:3003'

interface EnhancedTestPageProps {
    onClose: () => void
}

interface TestResult {
    timestamp: string
    action: string
    success: boolean
    response: string
    duration: number
}

const EnhancedTestPage: React.FC<EnhancedTestPageProps> = ({ onClose }) => {
    const [sensorTime, setSensorTime] = useState<string>('')
    const [customJson, setCustomJson] = useState<string>('')
    const [response, setResponse] = useState<string>('')
    const [loading, setLoading] = useState<boolean>(false)
    const [testResults, setTestResults] = useState<TestResult[]>([])
    const [sequenceRunning, setSequenceRunning] = useState<boolean>(false)
    const [activeCompetitor, setActiveCompetitor] = useState<any>(null)

    const getCurrentTimestamp = () => Date.now()

    // Check active competitor on load
    useEffect(() => {
        const checkActiveCompetitor = async () => {
            try {
                const response = await fetch(
                    `${API_BASE_URL}/api/active-competitor/`
                )
                if (response.ok) {
                    const data = await response.json()
                    setActiveCompetitor(data.active_competitor)
                }
            } catch (error) {
                console.error('Failed to fetch active competitor:', error)
            }
        }

        checkActiveCompetitor()
    }, [])

    const addTestResult = (
        action: string,
        success: boolean,
        response: string,
        duration: number
    ) => {
        const result: TestResult = {
            timestamp: new Date().toLocaleTimeString(),
            action,
            success,
            response:
                response.substring(0, 100) +
                (response.length > 100 ? '...' : ''),
            duration,
        }

        setTestResults((prev) => [result, ...prev.slice(0, 9)]) // Keep last 10 results
    }

    const sendTimingSignal = async (
        timeMark: 'START' | 'FINISH',
        customTime?: number
    ) => {
        const startTime = Date.now()
        setLoading(true)

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
            const duration = Date.now() - startTime

            if (response.ok) {
                const message = `✅ ${timeMark} signal sent successfully!\n${responseText}`
                setResponse(message)
                addTestResult(
                    `${timeMark} Signal`,
                    true,
                    responseText,
                    duration
                )
            } else {
                const message = `❌ Error: ${response.status}\n${responseText}`
                setResponse(message)
                addTestResult(
                    `${timeMark} Signal`,
                    false,
                    responseText,
                    duration
                )
            }
        } catch (error) {
            const duration = Date.now() - startTime
            const message = `❌ Network error: ${error}`
            setResponse(message)
            addTestResult(`${timeMark} Signal`, false, String(error), duration)
        } finally {
            setLoading(false)
        }
    }

    const sendCustomJson = async () => {
        if (!customJson.trim()) return

        const startTime = Date.now()
        setLoading(true)

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
            const duration = Date.now() - startTime

            if (response.ok) {
                const message = `✅ Custom payload sent successfully!\n${responseText}`
                setResponse(message)
                addTestResult('Custom JSON', true, responseText, duration)
            } else {
                const message = `❌ Error: ${response.status}\n${responseText}`
                setResponse(message)
                addTestResult('Custom JSON', false, responseText, duration)
            }
        } catch (error) {
            const duration = Date.now() - startTime
            const message = `❌ Error: ${error}`
            setResponse(message)
            addTestResult('Custom JSON', false, String(error), duration)
        } finally {
            setLoading(false)
        }
    }

    const simulateCompleteRun = async () => {
        setSequenceRunning(true)
        setResponse('🏁 Simulating complete timing sequence...\n\n')

        try {
            // Send START signal
            setResponse((prev) => prev + '⏱️ Sending START signal...\n')
            await sendTimingSignal('START')

            // Wait 3 seconds
            setResponse((prev) => prev + '⏳ Waiting 3 seconds...\n')
            await new Promise((resolve) => setTimeout(resolve, 3000))

            // Send FINISH signal
            setResponse((prev) => prev + '🏁 Sending FINISH signal...\n')
            await sendTimingSignal('FINISH')

            setResponse(
                (prev) => prev + '\n✅ Complete run simulation finished!'
            )
        } catch (error) {
            setResponse((prev) => prev + `\n❌ Sequence failed: ${error}`)
        } finally {
            setSequenceRunning(false)
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
            const startTime = Date.now()
            try {
                const response = await fetch(`${API_BASE_URL}${endpoint.url}`)
                const data = await response.text()
                const duration = Date.now() - startTime

                setResponse(
                    (prev) =>
                        prev +
                        `${endpoint.name}: ${response.status} ${response.ok ? '✅' : '❌'} (${duration}ms)\n`
                )
                addTestResult(
                    `API Test: ${endpoint.name}`,
                    response.ok,
                    `Status: ${response.status}`,
                    duration
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
                const duration = Date.now() - startTime
                setResponse(
                    (prev) => prev + `${endpoint.name}: Network Error ❌\n`
                )
                addTestResult(
                    `API Test: ${endpoint.name}`,
                    false,
                    String(error),
                    duration
                )
            }
        }

        setResponse((prev) => prev + '\nAPI test completed!')
        setLoading(false)
    }

    const quickTests = [
        {
            name: 'START Signal',
            action: () => sendTimingSignal('START'),
            color: 'success' as const,
            disabled: loading || sequenceRunning,
        },
        {
            name: 'FINISH Signal',
            action: () => sendTimingSignal('FINISH'),
            color: 'primary' as const,
            disabled: loading || sequenceRunning,
        },
        {
            name: 'Complete Run',
            action: simulateCompleteRun,
            color: 'secondary' as const,
            disabled: loading || sequenceRunning,
        },
        {
            name: 'START (5s ago)',
            action: () =>
                sendTimingSignal('START', getCurrentTimestamp() - 5000),
            color: 'info' as const,
            disabled: loading || sequenceRunning,
        },
    ]

    return (
        <Box sx={{ p: 3, maxWidth: 1400, mx: 'auto' }}>
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
                        🧪 Enhanced Sensor Testing Interface
                    </Typography>
                    <Button variant="outlined" onClick={onClose}>
                        Back to Dashboard
                    </Button>
                </Box>

                <Alert
                    severity={activeCompetitor ? 'success' : 'warning'}
                    sx={{ mb: 3 }}
                >
                    {activeCompetitor
                        ? `✅ Active Competitor: #${activeCompetitor.starting_order} ${activeCompetitor.participant.name}`
                        : '⚠️ No active competitor selected! Please select a competitor in the main dashboard first.'}
                </Alert>

                {sequenceRunning && (
                    <Box sx={{ mb: 2 }}>
                        <LinearProgress />
                        <Typography variant="caption" color="text.secondary">
                            Running sequence test...
                        </Typography>
                    </Box>
                )}

                <Grid container spacing={3}>
                    {/* Quick Test Buttons */}
                    <Grid item xs={12} lg={6}>
                        <Card>
                            <CardContent>
                                <Typography variant="h6" gutterBottom>
                                    🚀 Quick Tests
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
                                            onClick={test.action}
                                            disabled={test.disabled}
                                            size="small"
                                        >
                                            {test.name}
                                        </Button>
                                    ))}
                                </Box>

                                <Divider sx={{ my: 2 }} />

                                <Typography variant="subtitle2" gutterBottom>
                                    🎛️ Custom Timing
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
                                <Box sx={{ display: 'flex', gap: 1, mb: 2 }}>
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
                                        disabled={loading || sequenceRunning}
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
                                        disabled={loading || sequenceRunning}
                                        size="small"
                                    >
                                        Custom FINISH
                                    </Button>
                                </Box>

                                <Button
                                    variant="outlined"
                                    color="info"
                                    onClick={testApiEndpoints}
                                    disabled={loading || sequenceRunning}
                                    fullWidth
                                    size="small"
                                >
                                    🔍 Test All API Endpoints
                                </Button>
                            </CardContent>
                        </Card>
                    </Grid>

                    {/* Test Results History */}
                    <Grid item xs={12} lg={6}>
                        <Card>
                            <CardContent>
                                <Typography variant="h6" gutterBottom>
                                    📊 Test Results History
                                </Typography>

                                <List dense>
                                    {testResults.length > 0 ? (
                                        testResults.map((result, index) => (
                                            <ListItem key={index}>
                                                <ListItemIcon>
                                                    <Chip
                                                        label={
                                                            result.success
                                                                ? '✅'
                                                                : '❌'
                                                        }
                                                        size="small"
                                                        color={
                                                            result.success
                                                                ? 'success'
                                                                : 'error'
                                                        }
                                                    />
                                                </ListItemIcon>
                                                <ListItemText
                                                    primary={`${result.action} (${result.duration}ms)`}
                                                    secondary={`${result.timestamp} - ${result.response}`}
                                                    secondaryTypographyProps={{
                                                        style: {
                                                            fontSize: '0.75rem',
                                                        },
                                                    }}
                                                />
                                            </ListItem>
                                        ))
                                    ) : (
                                        <ListItem>
                                            <ListItemText
                                                primary="No tests run yet"
                                                secondary="Run some tests to see results here"
                                            />
                                        </ListItem>
                                    )}
                                </List>
                            </CardContent>
                        </Card>
                    </Grid>

                    {/* Custom JSON */}
                    <Grid item xs={12}>
                        <Card>
                            <CardContent>
                                <Typography variant="h6" gutterBottom>
                                    🔧 Custom JSON Payload
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

                                <Box
                                    sx={{
                                        display: 'flex',
                                        gap: 1,
                                        mb: 2,
                                        flexWrap: 'wrap',
                                    }}
                                >
                                    <Button
                                        variant="contained"
                                        onClick={sendCustomJson}
                                        disabled={
                                            loading ||
                                            !customJson.trim() ||
                                            sequenceRunning
                                        }
                                    >
                                        📤 Send Custom
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
                                        📝 Template START
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
                                        📝 Template FINISH
                                    </Button>
                                    <Button
                                        variant="outlined"
                                        onClick={() => setCustomJson('')}
                                    >
                                        🗑️ Clear
                                    </Button>
                                </Box>
                            </CardContent>
                        </Card>
                    </Grid>

                    {/* Response Display */}
                    <Grid item xs={12}>
                        <Card>
                            <CardContent>
                                <Typography variant="h6" gutterBottom>
                                    📟 Server Response
                                </Typography>

                                <Box
                                    sx={{
                                        display: 'flex',
                                        gap: 1,
                                        mb: 2,
                                        flexWrap: 'wrap',
                                    }}
                                >
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
                                    <Chip
                                        label={`WebSocket: ${process.env.REACT_APP_API_WEBSOCKET_URL}`}
                                        variant="outlined"
                                        size="small"
                                    />
                                </Box>

                                <TextField
                                    value={response}
                                    multiline
                                    rows={10}
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
                            ⏳ Sending request...
                        </Typography>
                    </Box>
                )}
            </Paper>
        </Box>
    )
}

export default EnhancedTestPage
