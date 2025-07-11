import React from 'react'
import {
    List,
    ListItem,
    ListItemButton,
    ListItemText,
    ListItemIcon,
    Paper,
    Typography,
    Box,
    Chip,
    Divider,
} from '@mui/material'
import {
    PersonOutline,
    EmojiEvents,
    Timer,
    CheckCircleOutline,
    RadioButtonUncheckedOutlined,
} from '@mui/icons-material'
import { CompetitionStart } from '../types'

interface CompetitorListProps {
    competitionStarts: CompetitionStart[]
    activeCompetitor: CompetitionStart | null
    onSelectCompetitor: (competitionStart: CompetitionStart) => void
    timings: {
        [key: string]: { start?: number; finish?: number; elapsed?: number }
    }
}

const CompetitorList: React.FC<CompetitorListProps> = ({
    competitionStarts,
    activeCompetitor,
    onSelectCompetitor,
    timings,
}) => {
    const formatTime = (timeMs?: number): string => {
        if (!timeMs) return '--:--:--'

        const minutes = Math.floor(timeMs / 60000)
        const seconds = Math.floor((timeMs % 60000) / 1000)
        const milliseconds = Math.floor((timeMs % 1000) / 10)

        return `${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}.${milliseconds.toString().padStart(2, '0')}`
    }

    const getStatusIcon = (competitionStart: CompetitionStart) => {
        if (competitionStart.has_finished) {
            return <CheckCircleOutline color="success" />
        } else if (
            competitionStart.has_started ||
            competitionStart.id === activeCompetitor?.id
        ) {
            return <Timer color="primary" />
        } else {
            return <RadioButtonUncheckedOutlined color="disabled" />
        }
    }

    const getStatusChip = (competitionStart: CompetitionStart) => {
        if (competitionStart.has_finished) {
            return <Chip label="Finished" color="success" size="small" />
        } else if (
            competitionStart.has_started ||
            competitionStart.id === activeCompetitor?.id
        ) {
            return <Chip label="Active" color="primary" size="small" />
        } else {
            return <Chip label="Waiting" color="default" size="small" />
        }
    }

    return (
        <Paper
            elevation={3}
            sx={{
                height: '100vh',
                overflow: 'auto',
                borderRadius: 2,
            }}
        >
            <Box sx={{ p: 2, bgcolor: 'primary.main', color: 'white' }}>
                <Typography
                    variant="h6"
                    sx={{ display: 'flex', alignItems: 'center', gap: 1 }}
                >
                    <EmojiEvents />
                    Competitors
                </Typography>
            </Box>

            <List sx={{ p: 0 }}>
                {competitionStarts.map((competitionStart, index) => {
                    const isActive =
                        activeCompetitor?.id === competitionStart.id
                    const timing = timings[competitionStart.id]

                    return (
                        <React.Fragment key={competitionStart.id}>
                            <ListItem disablePadding>
                                <ListItemButton
                                    selected={isActive}
                                    onClick={() =>
                                        onSelectCompetitor(competitionStart)
                                    }
                                    sx={{
                                        py: 2,
                                        px: 2,
                                        '&.Mui-selected': {
                                            bgcolor: 'primary.light',
                                            '&:hover': {
                                                bgcolor: 'primary.light',
                                            },
                                        },
                                    }}
                                >
                                    <ListItemIcon>
                                        {getStatusIcon(competitionStart)}
                                    </ListItemIcon>

                                    <ListItemText
                                        primary={
                                            <Box
                                                sx={{
                                                    display: 'flex',
                                                    alignItems: 'center',
                                                    gap: 1,
                                                    mb: 0.5,
                                                }}
                                            >
                                                <Typography
                                                    variant="subtitle1"
                                                    fontWeight="bold"
                                                >
                                                    #
                                                    {
                                                        competitionStart.starting_order
                                                    }{' '}
                                                    {
                                                        competitionStart
                                                            .participant.name
                                                    }
                                                </Typography>
                                                {getStatusChip(
                                                    competitionStart
                                                )}
                                            </Box>
                                        }
                                        secondary={
                                            <Box>
                                                <Typography
                                                    variant="caption"
                                                    color="text.secondary"
                                                >
                                                    {competitionStart
                                                        .participant
                                                        .hobby_horses.length > 0
                                                        ? competitionStart
                                                              .participant
                                                              .hobby_horses[0]
                                                              .name
                                                        : 'No horse assigned'}
                                                </Typography>
                                                {timing && (
                                                    <Box sx={{ mt: 0.5 }}>
                                                        <Typography
                                                            variant="caption"
                                                            color="text.secondary"
                                                        >
                                                            Time:{' '}
                                                            {formatTime(
                                                                timing.elapsed
                                                            )}
                                                        </Typography>
                                                        {competitionStart.penalty_points >
                                                            0 && (
                                                            <Typography
                                                                variant="caption"
                                                                color="error.main"
                                                                sx={{ ml: 1 }}
                                                            >
                                                                Penalties:{' '}
                                                                {
                                                                    competitionStart.penalty_points
                                                                }
                                                            </Typography>
                                                        )}
                                                    </Box>
                                                )}
                                            </Box>
                                        }
                                    />
                                </ListItemButton>
                            </ListItem>
                            {index < competitionStarts.length - 1 && (
                                <Divider />
                            )}
                        </React.Fragment>
                    )
                })}
            </List>

            {competitionStarts.length === 0 && (
                <Box sx={{ p: 3, textAlign: 'center' }}>
                    <PersonOutline
                        sx={{ fontSize: 48, color: 'text.disabled', mb: 1 }}
                    />
                    <Typography variant="body2" color="text.secondary">
                        No competitors found
                    </Typography>
                    <Typography variant="caption" color="text.secondary">
                        Add competitors in the admin panel
                    </Typography>
                </Box>
            )}
        </Paper>
    )
}

export default CompetitorList
