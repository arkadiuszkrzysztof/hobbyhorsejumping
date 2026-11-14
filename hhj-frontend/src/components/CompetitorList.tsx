import { useRef, useState, type FormEvent, type KeyboardEvent } from 'react'
import {
    DndContext,
    KeyboardSensor,
    PointerSensor,
    closestCenter,
    useSensor,
    useSensors,
    type DragEndEvent,
} from '@dnd-kit/core'
import {
    SortableContext,
    arrayMove,
    sortableKeyboardCoordinates,
    useSortable,
    verticalListSortingStrategy,
} from '@dnd-kit/sortable'
import type { Entry, Run } from '../api/types'
import { sortPenalties } from '../lib/fences'
import { formatTime } from '../lib/time'
import { useStore } from '../state/context'
import { useAsk, useConfirm } from './AskDialog'

export function runChipText(run: Run): string {
    if (run.status === 'OPEN') return 'live'
    return `${formatTime(run.time_ms)} · ${run.penalties.length} pen`
}

export function CompetitorList() {
    const [snap, store] = useStore()
    const state = snap.state
    const ask = useAsk()
    const sensors = useSensors(
        useSensor(PointerSensor, { activationConstraint: { distance: 4 } }),
        useSensor(KeyboardSensor, { coordinateGetter: sortableKeyboardCoordinates })
    )
    if (!state) return <aside className="competitors" />

    const ids = state.entries.map((e) => e.id)
    const ordering = snap.pending.has('order')
    const sessionEntryId = state.session?.entry_id ?? null

    const move = (from: number, to: number) => {
        if (to < 0 || to >= ids.length || from === to) return
        void store.reorder(arrayMove(ids, from, to))
    }

    const onDragEnd = (ev: DragEndEvent) => {
        if (!ev.over || ev.active.id === ev.over.id) return
        move(ids.indexOf(String(ev.active.id)), ids.indexOf(String(ev.over.id)))
    }

    const select = async (entry: Entry) => {
        if (entry.id === state.selected_entry) return
        const r = await store.select(entry.id)
        if (r.ok || r.status !== 409) return
        const current = store.getSnapshot().state
        const holder = current?.entries.find((e) => e.id === current.session?.entry_id)
        if (!holder || holder.id === entry.id) {
            store.pushError(r.error)
            return
        }
        const choice = await ask({
            title: `Session open for ${holder.name} — move it to ${entry.name}?`,
            message: (
                <p>
                    Try {current?.session?.number} with everything captured so far (signals, penalties) will
                    belong to <strong>{entry.name}</strong> instead.
                </p>
            ),
            actions: [{ label: `Move to ${entry.name}`, value: 'move' }],
        })
        if (choice === 'move') await store.select(entry.id, true)
    }

    return (
        <aside className="competitors" aria-label="Competitors">
            <h2 className="section-title">
                Competitors <span className="muted">({state.entries.length})</span>
            </h2>
            <DndContext sensors={sensors} collisionDetection={closestCenter} onDragEnd={onDragEnd}>
                <SortableContext items={ids} strategy={verticalListSortingStrategy}>
                    <ol className="entry-list">
                        {state.entries.map((entry, i) => (
                            <EntryRow
                                key={entry.id}
                                entry={entry}
                                position={i + 1}
                                selected={entry.id === state.selected_entry}
                                hasSession={entry.id === sessionEntryId}
                                isFirst={i === 0}
                                isLast={i === state.entries.length - 1}
                                ordering={ordering}
                                onSelect={() => void select(entry)}
                                onMove={(delta) => move(i, i + delta)}
                            />
                        ))}
                    </ol>
                </SortableContext>
            </DndContext>
            {state.entries.length === 0 && <p className="empty">No competitors yet — add them below.</p>}
            <AddCompetitor />
        </aside>
    )
}

interface RowProps {
    entry: Entry
    position: number
    selected: boolean
    hasSession: boolean
    isFirst: boolean
    isLast: boolean
    ordering: boolean
    onSelect: () => void
    onMove: (delta: number) => void
}

function EntryRow({ entry, position, selected, hasSession, isFirst, isLast, ordering, onSelect, onMove }: RowProps) {
    const [snap, store] = useStore()
    const ask = useAsk()
    const confirm = useConfirm()
    const [editing, setEditing] = useState(false)
    const { attributes, listeners, setNodeRef, transform, transition, isDragging } = useSortable({
        id: entry.id,
        disabled: ordering,
    })
    const style = {
        transform: transform ? `translate3d(${transform.x}px, ${transform.y}px, 0)` : undefined,
        transition,
    }
    const savedRuns = entry.runs.filter((r) => r.status === 'SAVED').length

    const remove = async () => {
        const ok = await confirm(
            `Remove ${entry.name}?`,
            savedRuns > 0 ? (
                <p>
                    <strong>
                        {savedRuns} saved run{savedRuns === 1 ? '' : 's'} will be deleted with {entry.name}.
                    </strong>{' '}
                    This cannot be undone.
                </p>
            ) : (
                <p>{entry.name} has no saved runs.</p>
            ),
            'Remove',
            'danger'
        )
        if (ok) await store.removeEntry(entry.id)
    }

    const runMenu = async (run: Run) => {
        const pens = sortPenalties(run.penalties)
        const choice = await ask({
            title: `${entry.name} — try ${run.number}`,
            message: (
                <p>
                    Saved: <strong>{formatTime(run.time_ms)}</strong>,{' '}
                    {pens.length === 0 ? 'no penalties' : `penalties ${pens.join(', ')}`}.
                </p>
            ),
            actions: [
                { label: 'Reopen to correct', value: 'reopen' },
                { label: 'Delete run', value: 'delete', tone: 'danger' },
            ],
        })
        if (choice === 'reopen') {
            await store.reopen(run.id)
        } else if (choice === 'delete') {
            const ok = await confirm(
                `Delete try ${run.number} of ${entry.name}?`,
                <p>The time and penalties of this run are lost. This cannot be undone.</p>,
                'Delete run',
                'danger'
            )
            if (ok) await store.deleteRun(run.id)
        }
    }

    const classes = ['entry', selected && 'entry-selected', hasSession && 'entry-session', isDragging && 'entry-dragging']
    return (
        <li
            ref={setNodeRef}
            style={style}
            className={classes.filter(Boolean).join(' ')}
            aria-current={selected || undefined}
            onClick={(e) => {
                // The whole row is a click target; its own buttons / inputs handle themselves.
                if (!editing && !(e.target as HTMLElement).closest('button, input')) onSelect()
            }}
        >
            <button
                className="drag-handle"
                aria-label={`Drag to reorder ${entry.name}`}
                disabled={ordering}
                {...attributes}
                {...listeners}
            >
                ⠿
            </button>
            <span className="entry-pos">{position}</span>
            <div className="entry-body">
                {editing ? (
                    <RenameInput
                        initial={entry.name}
                        onDone={async (name) => {
                            if (name && name !== entry.name) await store.renameEntry(entry.id, name)
                            setEditing(false)
                        }}
                        onCancel={() => setEditing(false)}
                    />
                ) : (
                    <button
                        className="entry-name"
                        onClick={onSelect}
                        disabled={snap.pending.has('select')}
                        aria-pressed={selected}
                    >
                        <span className="entry-name-text">{entry.name}</span>
                        {selected && <span className="selected-tag">SELECTED</span>}
                    </button>
                )}
                {entry.runs.length > 0 && (
                    <ul className="chips" aria-label={`Runs of ${entry.name}`}>
                        {entry.runs.map((run) => (
                            <li key={run.id}>
                                {run.status === 'OPEN' ? (
                                    <span className="chip chip-live" title={`Try ${run.number} — open session`}>
                                        <span className="live-dot" aria-hidden="true" />
                                        {run.number}: live
                                    </span>
                                ) : (
                                    <button
                                        className="chip"
                                        title={`Try ${run.number} — click to reopen or delete`}
                                        disabled={snap.pending.has(`run:${run.id}`)}
                                        onClick={() => void runMenu(run)}
                                    >
                                        {runChipText(run)}
                                    </button>
                                )}
                            </li>
                        ))}
                    </ul>
                )}
            </div>
            <div className="entry-tools">
                <button className="tool" aria-label={`Move ${entry.name} up`} disabled={isFirst || ordering} onClick={() => onMove(-1)}>
                    ▲
                </button>
                <button className="tool" aria-label={`Move ${entry.name} down`} disabled={isLast || ordering} onClick={() => onMove(1)}>
                    ▼
                </button>
                <button className="tool" aria-label={`Rename ${entry.name}`} onClick={() => setEditing(true)} disabled={editing}>
                    ✎
                </button>
                <button
                    className="tool tool-danger"
                    aria-label={`Remove ${entry.name}`}
                    title={hasSession ? 'Save or discard the open session first' : undefined}
                    disabled={hasSession || snap.pending.has(`entry:remove:${entry.id}`)}
                    onClick={() => void remove()}
                >
                    ✕
                </button>
            </div>
        </li>
    )
}

function RenameInput({ initial, onDone, onCancel }: { initial: string; onDone: (name: string) => void; onCancel: () => void }) {
    const [value, setValue] = useState(initial)
    const done = useRef(false)
    const finish = (commit: boolean) => {
        if (done.current) return
        done.current = true
        if (commit) onDone(value.trim())
        else onCancel()
    }
    const onKey = (e: KeyboardEvent) => {
        if (e.key === 'Enter') finish(true)
        if (e.key === 'Escape') finish(false)
    }
    return (
        <input
            className="rename-input"
            aria-label="New name"
            autoFocus
            value={value}
            maxLength={100}
            onChange={(e) => setValue(e.target.value)}
            onKeyDown={onKey}
            onBlur={() => finish(true)}
        />
    )
}

function AddCompetitor() {
    const [snap, store] = useStore()
    const [name, setName] = useState('')
    const latest = useRef('')
    latest.current = name
    const adding = snap.pending.has('entry:add')

    // The input itself is never disabled: disabling would drop the focus, and the
    // operator wants to type several names in a row.
    const submit = async (e: FormEvent) => {
        e.preventDefault()
        const value = name.trim()
        if (!value || adding) return
        const r = await store.addEntry(value)
        if (r.ok && latest.current.trim() === value) setName('')
    }

    return (
        <form className="add-competitor" onSubmit={submit}>
            <input
                aria-label="New competitor name"
                placeholder="Add competitor — type name, Enter"
                value={name}
                maxLength={100}
                onChange={(e) => setName(e.target.value)}
            />
            <button className="btn" type="submit" disabled={adding || name.trim() === ''}>
                Add
            </button>
        </form>
    )
}
