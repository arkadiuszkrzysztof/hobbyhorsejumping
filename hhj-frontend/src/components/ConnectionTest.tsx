import React, { useState, useEffect } from 'react'
import {
    Box,
    Paper,
    Typography,
    Button,
    Alert,
    LinearProgress,
    Chip,
} from '@mui/material'

const API_BASE_URL =
    process.env.REACT_APP_API_HTTP_URL || 'http://127.0.0.1:3003'

const ConnectionTest: React.FC = () => {
    const [testing, setTesting] = useState(false)
    const [results, setResults] = useState<{
        [key: string]: {
            success: boolean
            message: string
            responseTime?: number
        }
    }>({})

    const testEndpoints = [
        { name: 'API Root', url: '/api/' },
        { name: 'Competitions', url: '/api/competitions/' },
        { name: 'Active Competitor', url: '/api/active-competitor/' },
        { name: 'Time Readings', url: '/timereadings/' },
    ]

    const testConnection = async () => {
        setTesting(true)
        setResults({})

        for (const endpoint of testEndpoints) {
            const startTime = Date.now()
            try {
                const response = await fetch(`${API_BASE_URL}${endpoint.url}`, {
                    method: 'GET',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                })

                const responseTime = Date.now() - startTime

                if (response.ok) {
                    setResults((prev) => ({
                        ...prev,
                        [endpoint.name]: {
                            success: true,
                            message: `✅ ${response.status} OK`,
                            responseTime,
                        },
                    }))
                } else {
                    setResults((prev) => ({
                        ...prev,
                        [endpoint.name]: {
                            success: false,
                            message: `❌ ${response.status} ${response.statusText}`,
                            responseTime,
                        },
                    }))
                }
            } catch (error) {
                const responseTime = Date.now() - startTime
                setResults((prev) => ({
                    ...prev,
                    [endpoint.name]: {
                        success: false,
                        message: `❌ Connection failed: ${error}`,
                        responseTime,
                    },
                }))
            }
        }

        setTesting(false)
    }

    useEffect(() => {
        testConnection()
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [])

    return (
        <Paper elevation={2} sx={{ p: 3, mb: 2 }}>
            <Typography variant="h6" gutterBottom>
                🔍 Backend Connection Test
            </Typography>

            <Box sx={{ mb: 2 }}>
                <Chip
                    label={`Testing: ${API_BASE_URL}`}
                    variant="outlined"
                    size="small"
                />
            </Box>

            {testing && (
                <Box sx={{ mb: 2 }}>
                    <LinearProgress />
                    <Typography variant="caption" color="text.secondary">
                        Testing endpoints...
                    </Typography>
                </Box>
            )}

            <Box sx={{ mb: 2 }}>
                {testEndpoints.map((endpoint) => {
                    const result = results[endpoint.name]
                    if (!result) return null

                    return (
                        <Alert
                            key={endpoint.name}
                            severity={result.success ? 'success' : 'error'}
                            sx={{ mb: 1 }}
                        >
                            <Box
                                sx={{
                                    display: 'flex',
                                    justifyContent: 'space-between',
                                    alignItems: 'center',
                                }}
                            >
                                <Box>
                                    <Typography
                                        variant="body2"
                                        fontWeight="bold"
                                    >
                                        {endpoint.name}
                                    </Typography>
                                    <Typography variant="caption">
                                        {endpoint.url}
                                    </Typography>
                                </Box>
                                <Box sx={{ textAlign: 'right' }}>
                                    <Typography variant="body2">
                                        {result.message}
                                    </Typography>
                                    {result.responseTime && (
                                        <Typography
                                            variant="caption"
                                            color="text.secondary"
                                        >
                                            {result.responseTime}ms
                                        </Typography>
                                    )}
                                </Box>
                            </Box>
                        </Alert>
                    )
                })}
            </Box>

            <Button
                variant="outlined"
                onClick={testConnection}
                disabled={testing}
                fullWidth
            >
                🔄 Test Again
            </Button>

            {Object.keys(results).length > 0 && (
                <Box sx={{ mt: 2 }}>
                    <Typography variant="caption" color="text.secondary">
                        💡 If connections fail, make sure:
                        <br />• Django server is running on port 3003
                        <br />• CORS is properly configured
                        <br />• Firewall is not blocking the connection
                    </Typography>
                </Box>
            )}
        </Paper>
    )
}

export default ConnectionTest
