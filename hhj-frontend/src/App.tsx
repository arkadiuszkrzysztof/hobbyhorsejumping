import React, { useEffect, useRef, useState } from 'react'
import useWebSocket, { ReadyState } from 'react-use-websocket'
import 'dotenv/config'

import { Box, Typography } from '@mui/material'

function App() {
    const [messageHistory, setMessageHistory] = useState<MessageEvent<any>[]>(
        []
    )
    const { sendMessage, lastMessage, readyState } = useWebSocket(
        `${process.env.REACT_APP_API_WEBSOCKET_URL}/ws/timings/`
    )

    const [startTime, setStartTime] = useState<number | null>(null)

    useEffect(() => {
        if (lastMessage !== null) {
            setMessageHistory((prev) => prev.concat(lastMessage))

            const data = JSON.parse(lastMessage.data)

            if (data.time_mark === 'START') {
                resetTimer()
                startTimer()
                setStartTime(data.sensor_time)
            } else if (data.time_mark === 'FINISH') {
                stopTimer()
                setTimer(data.sensor_time - startTime!)
            }
        }
    }, [lastMessage])

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
    }

    return (
        <Box sx={{ textAlign: 'center' }}>
            <Box sx={{ fontSize: '500px', fontFamily: 'monospace' }}>
                {minutes.toString().padStart(2, '0')}:
                {seconds.toString().padStart(2, '0')}
                <Typography
                    sx={{
                        color: 'teal',
                        fontSize: '300px',
                        fontFamily: 'monospace',
                        display: 'inline',
                    }}
                >
                    .{milliseconds.toString().padStart(2, '0')}
                </Typography>
            </Box>
            <button type="button" onClick={startTimer}>
                START
            </button>
            <button type="button" onClick={resetTimer}>
                RESET
            </button>
            <button type="button" onClick={stopTimer}>
                STOP
            </button>
            <Box>
                <span>The WebSocket is currently {connectionStatus}</span>
                {lastMessage ? (
                    <span>Last message: {lastMessage.data}</span>
                ) : null}
                <ul>
                    {messageHistory.map((message, idx) => (
                        <li key={idx}>{message ? message.data : null}</li>
                    ))}
                </ul>
            </Box>
        </Box>
    )
}

export default App
