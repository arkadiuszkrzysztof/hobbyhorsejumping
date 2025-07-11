import React, { useState } from 'react'
import {
    Box,
    Paper,
    Typography,
    List,
    ListItem,
    ListItemText,
    ListItemIcon,
    IconButton,
    Button,
    Alert,
    Divider,
} from '@mui/material'
import { DragIndicator } from '@mui/icons-material'
import { CompetitionStart } from '../types'

interface CompetitorReorderProps {
    competitionStarts: CompetitionStart[]
    onReorder: (reorderedCompetitors: CompetitionStart[]) => void
    onClose: () => void
    disabled?: boolean
}

const CompetitorReorder: React.FC<CompetitorReorderProps> = ({
    competitionStarts,
    onReorder,
    onClose,
    disabled = false,
}) => {
    const [competitors, setCompetitors] = useState<CompetitionStart[]>([
        ...competitionStarts,
    ])
    const [draggedIndex, setDraggedIndex] = useState<number | null>(null)

    const handleDragStart = (event: React.DragEvent, index: number) => {
        setDraggedIndex(index)
        event.dataTransfer.effectAllowed = 'move'
    }

    const handleDragOver = (event: React.DragEvent) => {
        event.preventDefault()
        event.dataTransfer.dropEffect = 'move'
    }

    const handleDrop = (event: React.DragEvent, dropIndex: number) => {
        event.preventDefault()

        if (draggedIndex === null || draggedIndex === dropIndex) {
            return
        }

        const newCompetitors = [...competitors]
        const draggedCompetitor = newCompetitors[draggedIndex]

        // Remove dragged item
        newCompetitors.splice(draggedIndex, 1)

        // Insert at new position
        newCompetitors.splice(dropIndex, 0, draggedCompetitor)

        // Update starting orders
        const updatedCompetitors = newCompetitors.map((competitor, index) => ({
            ...competitor,
            starting_order: index + 1,
        }))

        setCompetitors(updatedCompetitors)
        setDraggedIndex(null)
    }

    const moveUp = (index: number) => {
        if (index === 0) return

        const newCompetitors = [...competitors]
        ;[newCompetitors[index], newCompetitors[index - 1]] = [
            newCompetitors[index - 1],
            newCompetitors[index],
        ]

        // Update starting orders
        const updatedCompetitors = newCompetitors.map((competitor, idx) => ({
            ...competitor,
            starting_order: idx + 1,
        }))

        setCompetitors(updatedCompetitors)
    }

    const moveDown = (index: number) => {
        if (index === competitors.length - 1) return

        const newCompetitors = [...competitors]
        ;[newCompetitors[index], newCompetitors[index + 1]] = [
            newCompetitors[index + 1],
            newCompetitors[index],
        ]

        // Update starting orders
        const updatedCompetitors = newCompetitors.map((competitor, idx) => ({
            ...competitor,
            starting_order: idx + 1,
        }))

        setCompetitors(updatedCompetitors)
    }

    const handleSave = () => {
        onReorder(competitors)
    }

    const handleReset = () => {
        setCompetitors([...competitionStarts])
    }

    const hasChanges =
        JSON.stringify(competitors.map((c) => c.starting_order)) !==
        JSON.stringify(competitionStarts.map((c) => c.starting_order))

    return (
        <Paper elevation={3} sx={{ p: 3 }}>
            <Box
                sx={{
                    display: 'flex',
                    justifyContent: 'space-between',
                    alignItems: 'center',
                    mb: 2,
                }}
            >
                <Typography variant="h5">🔢 Reorder Starting List</Typography>
                <Button variant="outlined" onClick={onClose}>
                    ← Back
                </Button>
            </Box>

            <Alert severity="info" sx={{ mb: 2 }}>
                Drag and drop competitors to reorder the starting list, or use
                the arrow buttons. Changes will be synchronized across all
                connected displays.
            </Alert>

            {disabled && (
                <Alert severity="warning" sx={{ mb: 2 }}>
                    Reordering is disabled while competition is in progress.
                </Alert>
            )}

            <List sx={{ bgcolor: 'background.paper', borderRadius: 1 }}>
                {competitors.map((competitor, index) => (
                    <React.Fragment key={competitor.id}>
                        <ListItem
                            draggable={!disabled}
                            onDragStart={(e) => handleDragStart(e, index)}
                            onDragOver={handleDragOver}
                            onDrop={(e) => handleDrop(e, index)}
                            sx={{
                                cursor: disabled ? 'default' : 'grab',
                                '&:hover': disabled
                                    ? {}
                                    : { bgcolor: 'action.hover' },
                                '&:active': {
                                    cursor: disabled ? 'default' : 'grabbing',
                                },
                                opacity: draggedIndex === index ? 0.5 : 1,
                                transition: 'all 0.2s',
                                border: competitor.has_started
                                    ? '2px solid'
                                    : 'none',
                                borderColor: competitor.has_finished
                                    ? 'success.main'
                                    : 'warning.main',
                                borderRadius: 1,
                                mb: 0.5,
                            }}
                        >
                            <ListItemIcon>
                                <DragIndicator
                                    color={disabled ? 'disabled' : 'action'}
                                />
                            </ListItemIcon>

                            <ListItemText
                                primary={
                                    <Box
                                        sx={{
                                            display: 'flex',
                                            alignItems: 'center',
                                            gap: 1,
                                        }}
                                    >
                                        <Typography
                                            variant="h6"
                                            component="span"
                                            color="primary"
                                        >
                                            #{competitor.starting_order}
                                        </Typography>
                                        <Typography
                                            variant="body1"
                                            component="span"
                                        >
                                            {competitor.participant.name}
                                        </Typography>
                                        {competitor.has_started &&
                                            !competitor.has_finished && (
                                                <Typography
                                                    variant="caption"
                                                    color="warning.main"
                                                    sx={{ fontWeight: 'bold' }}
                                                >
                                                    ⏱️ IN PROGRESS
                                                </Typography>
                                            )}
                                        {competitor.has_finished && (
                                            <Typography
                                                variant="caption"
                                                color="success.main"
                                                sx={{ fontWeight: 'bold' }}
                                            >
                                                ✅ FINISHED
                                            </Typography>
                                        )}
                                    </Box>
                                }
                                secondary={
                                    competitor.participant.hobby_horses.length >
                                    0
                                        ? competitor.participant.hobby_horses[0]
                                              .name
                                        : 'No horse assigned'
                                }
                            />

                            <Box
                                sx={{
                                    display: 'flex',
                                    flexDirection: 'column',
                                    ml: 1,
                                }}
                            >
                                <IconButton
                                    size="small"
                                    onClick={() => moveUp(index)}
                                    disabled={disabled || index === 0}
                                    aria-label="Move up"
                                >
                                    ⬆️
                                </IconButton>
                                <IconButton
                                    size="small"
                                    onClick={() => moveDown(index)}
                                    disabled={
                                        disabled ||
                                        index === competitors.length - 1
                                    }
                                    aria-label="Move down"
                                >
                                    ⬇️
                                </IconButton>
                            </Box>
                        </ListItem>

                        {index < competitors.length - 1 && <Divider />}
                    </React.Fragment>
                ))}
            </List>

            <Box
                sx={{
                    display: 'flex',
                    gap: 2,
                    mt: 3,
                    justifyContent: 'flex-end',
                }}
            >
                <Button
                    variant="outlined"
                    onClick={handleReset}
                    disabled={disabled || !hasChanges}
                >
                    🔄 Reset
                </Button>
                <Button
                    variant="contained"
                    onClick={handleSave}
                    disabled={disabled || !hasChanges}
                    color="primary"
                >
                    💾 Save Order
                </Button>
            </Box>

            {hasChanges && (
                <Alert severity="warning" sx={{ mt: 2 }}>
                    You have unsaved changes. Click "Save Order" to apply the
                    new starting order.
                </Alert>
            )}
        </Paper>
    )
}

export default CompetitorReorder
