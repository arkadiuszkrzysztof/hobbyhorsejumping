import { useState, type FormEvent } from 'react'

export const MIN_FENCES = 1
export const MAX_FENCES = 24

interface Props {
    initialName?: string
    initialFences?: number
    submitLabel: string
    busy?: boolean
    onSubmit: (name: string, fenceCount: number) => void
    onCancel?: () => void
}

export function CompetitionForm({ initialName = '', initialFences = 12, submitLabel, busy, onSubmit, onCancel }: Props) {
    const [name, setName] = useState(initialName)
    const [fences, setFences] = useState(String(initialFences))
    const count = Number(fences)
    const valid = name.trim() !== '' && Number.isInteger(count) && count >= MIN_FENCES && count <= MAX_FENCES

    const submit = (e: FormEvent) => {
        e.preventDefault()
        if (valid && !busy) onSubmit(name.trim(), count)
    }

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
            <label className="fences-field">
                <span>Fences</span>
                <input
                    type="number"
                    min={MIN_FENCES}
                    max={MAX_FENCES}
                    value={fences}
                    onChange={(e) => setFences(e.target.value)}
                />
            </label>
            <div className="form-actions">
                <button className="btn btn-primary" type="submit" disabled={!valid || busy}>
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
