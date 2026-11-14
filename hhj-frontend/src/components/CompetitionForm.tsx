import { useState, type FormEvent } from 'react'
import type { CompetitionBody } from '../api/client'
import { formatPoints, parsePoints, parseTimeAllowed } from '../lib/points'

export const MIN_FENCES = 1
export const MAX_FENCES = 24

/** What a new competition starts with: the usual show-jumping values. */
const USUAL = { fence_count: 12, time_allowed: null, knockdown_points: 4, refusal_points: 4, overtime_points: 0.4 }

interface Props {
    initial?: Partial<CompetitionBody>
    submitLabel: string
    busy?: boolean
    onSubmit: (competition: CompetitionBody) => void
    onCancel?: () => void
}

export function CompetitionForm({ initial = {}, submitLabel, busy, onSubmit, onCancel }: Props) {
    const start = { ...USUAL, name: '', ...initial }
    const [name, setName] = useState(start.name)
    const [fences, setFences] = useState(String(start.fence_count))
    const [allowed, setAllowed] = useState(start.time_allowed === null ? '' : String(start.time_allowed))
    const [knockdown, setKnockdown] = useState(formatPoints(start.knockdown_points))
    const [refusal, setRefusal] = useState(formatPoints(start.refusal_points))
    const [overtime, setOvertime] = useState(formatPoints(start.overtime_points))

    const count = Number(fences)
    const fencesOk = /^\d+$/.test(fences.trim()) && count >= MIN_FENCES && count <= MAX_FENCES
    const time_allowed = parseTimeAllowed(allowed)
    const knockdown_points = parsePoints(knockdown)
    const refusal_points = parsePoints(refusal)
    const overtime_points = parsePoints(overtime)
    const competition: CompetitionBody | null =
        name.trim() !== '' &&
        fencesOk &&
        time_allowed !== undefined &&
        knockdown_points !== null &&
        refusal_points !== null &&
        overtime_points !== null
            ? { name: name.trim(), fence_count: count, time_allowed, knockdown_points, refusal_points, overtime_points }
            : null

    const submit = (e: FormEvent) => {
        e.preventDefault()
        if (competition && !busy) onSubmit(competition)
    }

    const points = (label: string, value: string, set: (v: string) => void, ok: boolean) => (
        <label className="short-field">
            <span>{label}</span>
            <input inputMode="decimal" value={value} aria-invalid={!ok} onChange={(e) => set(e.target.value)} />
        </label>
    )

    return (
        <form className="competition-form" onSubmit={submit}>
            <label>
                <span>Name</span>
                <input
                    autoFocus
                    value={name}
                    onChange={(e) => setName(e.target.value)}
                    placeholder="e.g. Saturday open"
                    maxLength={100}
                />
            </label>
            <label className="short-field">
                <span>Fences</span>
                <input
                    type="number"
                    min={MIN_FENCES}
                    max={MAX_FENCES}
                    value={fences}
                    aria-invalid={!fencesOk}
                    onChange={(e) => setFences(e.target.value)}
                />
            </label>
            <label className="short-field">
                <span>Time allowed (s)</span>
                <input
                    inputMode="numeric"
                    value={allowed}
                    placeholder="none"
                    aria-invalid={time_allowed === undefined}
                    title="The time allowed for the course (norma czasu), in whole seconds. Empty: no time penalties."
                    onChange={(e) => setAllowed(e.target.value)}
                />
            </label>
            <fieldset className="points-fields">
                <legend>Penalty points for a</legend>
                {points('Knockdown', knockdown, setKnockdown, knockdown_points !== null)}
                {points('Refusal', refusal, setRefusal, refusal_points !== null)}
                {points('Second over time', overtime, setOvertime, overtime_points !== null)}
            </fieldset>
            <div className="form-actions">
                <button className="btn btn-primary" type="submit" disabled={!competition || busy}>
                    {submitLabel}
                </button>
                {onCancel && (
                    <button className="btn" type="button" onClick={onCancel}>
                        Cancel
                    </button>
                )}
            </div>
        </form>
    )
}
