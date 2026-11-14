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
import { ranking } from '../lib/ranking'
import { formatTime } from '../lib/time'
import { useStore } from '../state/context'
import { useAsk, useConfirm } from './AskDialog'
import { HorseName } from './HorseName'

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
    const ranked = snap.listOrder === 'ranking'
    // One row per competitor: its number is the start position, or the place in the ranking.
    const rows = ranked
        ? ranking(state.entries).map((r) => ({ entry: r.entry, position: r.place, bestRunId: r.result?.run.id ?? null }))
        : state.entries.map((entry, i) => ({ entry, position: i + 1, bestRunId: null }))
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
            <div className="list-head">
                <h2 className="section-title">
                    Competitors <span className="muted">({state.entries.length})</span>
                </h2>
                <div className="segmented" role="group" aria-label="Sort competitors">
                    <button aria-pressed={!ranked} onClick={() => store.setListOrder('start')}>
                        Start order
                    </button>
                    <button aria-pressed={ranked} onClick={() => store.setListOrder('ranking')}>
                        Ranking
                    </button>
                </div>
            </div>
            {ranked && <p className="list-note">Fewest penalties first, then the fastest time. The best run counts.</p>}
            <DndContext sensors={sensors} collisionDetection={closestCenter} onDragEnd={onDragEnd}>
                <SortableContext items={rows.map((r) => r.entry.id)} strategy={verticalListSortingStrategy}>
                    <ol className="entry-list">
                        {rows.map(({ entry, position, bestRunId }) => {
                            const i = ids.indexOf(entry.id)
                            return (
                                <EntryRow
                                    key={entry.id}
                                    entry={entry}
                                    position={position}
                                    bestRunId={bestRunId}
                                    reorderable={!ranked}
                                    selected={entry.id === state.selected_entry}
                                    hasSession={entry.id === sessionEntryId}
                                    isFirst={i === 0}
                                    isLast={i === ids.length - 1}
                                    ordering={ordering}
                                    onSelect={() => void select(entry)}
                                    onMove={(delta) => move(i, i + delta)}
                                />
                            )
                        })}
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
    /** Start position or place in the ranking; null = not ranked yet. */
    position: number | null
    /** The run that counts in the ranking, to mark its chip. */
    bestRunId: string | null
    /** False in the ranking: the order is not the operator's to change there. */
    reorderable: boolean
    selected: boolean
    hasSession: boolean
    isFirst: boolean
    isLast: boolean
    ordering: boolean
    onSelect: () => void
    onMove: (delta: number) => void
}

function EntryRow({
    entry,
    position,
    bestRunId,
    reorderable,
    selected,
    hasSession,
    isFirst,
    isLast,
    ordering,
    onSelect,
    onMove,
}: RowProps) {
    const [snap, store] = useStore()
    const ask = useAsk()
    const confirm = useConfirm()
    const [editing, setEditing] = useState(false)
    const { attributes, listeners, setNodeRef, transform, transition, isDragging } = useSortable({
        id: entry.id,
        disabled: ordering || !reorderable,
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

    const classes = [
        'entry',
        !reorderable && 'entry-ranked',
        selected && 'entry-selected',
        hasSession && 'entry-session',
        isDragging && 'entry-dragging',
    ]
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
            {reorderable && (
                <button
                    className="drag-handle"
                    aria-label={`Drag to reorder ${entry.name}`}
                    disabled={ordering}
                    {...attributes}
                    {...listeners}
                >
                    ⠿
                </button>
            )}
            <span className="entry-pos" title={reorderable ? undefined : position === null ? 'No result yet' : `Place ${position}`}>
                {position ?? '–'}
            </span>
            <div className="entry-body">
                {editing ? (
                    <EditEntry
                        entry={entry}
                        onDone={async (name, horse) => {
                            // An emptied name is not a rename; an emptied horse removes the horse.
                            const patch = {
                                ...(name && name !== entry.name ? { name } : {}),
                                ...(horse !== entry.horse ? { horse } : {}),
                            }
                            if (Object.keys(patch).length > 0) await store.updateEntry(entry.id, patch)
                            setEditing(false)
                        }}
                        onCancel={() => setEditing(false)}
                    />
                ) : (
                    <>
                        <button
                            className="entry-name"
                            onClick={onSelect}
                            disabled={snap.pending.has('select')}
                            aria-pressed={selected}
                        >
                            <span className="entry-name-text">{entry.name}</span>
                            {selected && <span className="selected-tag">SELECTED</span>}
                        </button>
                        <HorseName name={entry.horse} className="entry-horse" />
                    </>
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
                                        className={run.id === bestRunId ? 'chip chip-best' : 'chip'}
                                        title={
                                            run.id === bestRunId
                                                ? `Try ${run.number} — counts in the ranking; click to reopen or delete`
                                                : `Try ${run.number} — click to reopen or delete`
                                        }
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
                {reorderable && (
                    <>
                        <button
                            className="tool"
                            aria-label={`Move ${entry.name} up`}
                            disabled={isFirst || ordering}
                            onClick={() => onMove(-1)}
                        >
                            ▲
                        </button>
                        <button
                            className="tool"
                            aria-label={`Move ${entry.name} down`}
                            disabled={isLast || ordering}
                            onClick={() => onMove(1)}
                        >
                            ▼
                        </button>
                    </>
                )}
                <button
                    className="tool"
                    aria-label={`Edit ${entry.name}`}
                    title="Correct the name or the horse"
                    onClick={() => setEditing(true)}
                    disabled={editing}
                >
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

/** Name and horse of a competitor, edited in place: Enter or leaving both fields saves, Escape cancels. */
function EditEntry({
    entry,
    onDone,
    onCancel,
}: {
    entry: Entry
    onDone: (name: string, horse: string) => void
    onCancel: () => void
}) {
    const [name, setName] = useState(entry.name)
    const [horse, setHorse] = useState(entry.horse)
    const done = useRef(false)
    const finish = (commit: boolean) => {
        if (done.current) return
        done.current = true
        if (commit) onDone(name.trim(), horse.trim())
        else onCancel()
    }
    const onKey = (e: KeyboardEvent) => {
        if (e.key === 'Enter') finish(true)
        if (e.key === 'Escape') finish(false)
    }
    return (
        <div
            className="edit-entry"
            onBlur={(e) => {
                // moving between the two fields is not leaving
                if (!e.currentTarget.contains(e.relatedTarget)) finish(true)
            }}
        >
            <input
                className="rename-input"
                aria-label="Competitor name"
                autoFocus
                value={name}
                maxLength={100}
                onChange={(e) => setName(e.target.value)}
                onKeyDown={onKey}
            />
            <input
                className="rename-input rename-horse"
                aria-label="Horse name"
                placeholder="Horse (optional)"
                value={horse}
                maxLength={100}
                onChange={(e) => setHorse(e.target.value)}
                onKeyDown={onKey}
            />
        </div>
    )
}

function AddCompetitor() {
    const [snap, store] = useStore()
    const [name, setName] = useState('')
    const [horse, setHorse] = useState('')
    const nameInput = useRef<HTMLInputElement>(null)
    const horseInput = useRef<HTMLInputElement>(null)
    const latest = useRef({ name, horse })
    latest.current = { name, horse }
    const adding = snap.pending.has('entry:add')

    // The inputs are never disabled: disabling would drop the focus, and the
    // operator wants to type several competitors in a row.
    const submit = async (e: FormEvent) => {
        e.preventDefault()
        const sent = { name: name.trim(), horse: horse.trim() }
        if (!sent.name || adding) return
        const fromHorse = document.activeElement === horseInput.current
        const r = await store.addEntry(sent.name, sent.horse)
        if (!r.ok) return
        // Clear what was sent (not what has been typed since) and be ready for the next name.
        if (latest.current.name.trim() === sent.name) setName('')
        if (latest.current.horse.trim() === sent.horse) setHorse('')
        if (fromHorse && document.activeElement === horseInput.current) nameInput.current?.focus()
    }

    return (
        <form className="add-competitor" onSubmit={submit}>
            <input
                ref={nameInput}
                aria-label="New competitor name"
                placeholder="Add competitor — name, Enter"
                value={name}
                maxLength={100}
                onChange={(e) => setName(e.target.value)}
            />
            <div className="add-row">
                <input
                    ref={horseInput}
                    aria-label="Horse name (optional)"
                    placeholder="Horse (optional)"
                    value={horse}
                    maxLength={100}
                    onChange={(e) => setHorse(e.target.value)}
                />
                <button className="btn" type="submit" disabled={adding || name.trim() === ''}>
                    Add
                </button>
            </div>
        </form>
    )
}
