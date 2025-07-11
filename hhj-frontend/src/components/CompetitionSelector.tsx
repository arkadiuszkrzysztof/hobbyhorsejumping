import React from 'react'
import {
    FormControl,
    InputLabel,
    Select,
    MenuItem,
    Box,
    Typography,
    Chip,
} from '@mui/material'
import { Competition } from '../types'

interface CompetitionSelectorProps {
    competitions: Competition[]
    selectedCompetition: Competition | null
    onSelectCompetition: (competition: Competition) => void
}

const CompetitionSelector: React.FC<CompetitionSelectorProps> = ({
    competitions,
    selectedCompetition,
    onSelectCompetition,
}) => {
    const formatDateTime = (dateTime: string): string => {
        return new Date(dateTime).toLocaleString()
    }

    return (
        <Box sx={{ mb: 2 }}>
            <FormControl fullWidth>
                <InputLabel id="competition-select-label">
                    Select Competition
                </InputLabel>
                <Select
                    labelId="competition-select-label"
                    id="competition-select"
                    value={selectedCompetition?.id || ''}
                    label="Select Competition"
                    onChange={(e) => {
                        const competition = competitions.find(
                            (c) => c.id === e.target.value
                        )
                        if (competition) {
                            onSelectCompetition(competition)
                        }
                    }}
                >
                    {competitions.map((competition) => (
                        <MenuItem key={competition.id} value={competition.id}>
                            <Box>
                                <Typography variant="subtitle1">
                                    {competition.name}
                                </Typography>
                                <Typography
                                    variant="caption"
                                    color="text.secondary"
                                >
                                    {competition.event.name} •{' '}
                                    {formatDateTime(
                                        competition.start_date_time
                                    )}
                                </Typography>
                                <Box sx={{ mt: 0.5 }}>
                                    <Chip
                                        label={competition.height_class}
                                        size="small"
                                        color="primary"
                                        sx={{ mr: 1 }}
                                    />
                                    <Chip
                                        label={`${competition.time_limit}s`}
                                        size="small"
                                        color="secondary"
                                    />
                                </Box>
                            </Box>
                        </MenuItem>
                    ))}
                </Select>
            </FormControl>
        </Box>
    )
}

export default CompetitionSelector
