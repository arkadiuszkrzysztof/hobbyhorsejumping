import { useEffect, useState } from 'react'
import type { RoundEnd } from '../api/client'
import { fenceRows } from '../lib/fences'
import { pointsText } from '../lib/points'
import { formatTime } from '../lib/time'
import { useStore } from '../state/context'
import { useConfirm } from './AskDialog'

type Ending = RoundEnd['outcome']

const ENDING: Record<Ending, { verb: string; done: string; why: string }> = {
    RETIRED: { verb: 'Retire', done: 'retired', why: 'The competitor gave up on the way.' },
    ELIMINATED: { verb: 'Eliminate', done: 'eliminated', why: 'The round was ended for the competitor, e.g. after a fall.' },
}

export function SaveBar() {
    const [snap, store] = useStore()
    const confirm = useConfirm()
    const [ending, setEnding] = useState<Ending | null>(null)
    const state = snap.state!
    const session = state.session
    const entry = state.entries.find((e) => e.id === session?.entry_id)
    const busy = snap.pending.has('save')

    const save = async () => {
        if (!session) return
        if (session.time_ms === null) {
            const ok = await confirm(
                'Save without a time?',
                <p>
                    No START/FINISH pair counts for {entry?.name ?? 'this competitor'}. The run is saved as completed,
                    with no time and {pointsText(session)}. If the round was not completed, use Retire or Eliminate
                    instead.
                </p>,
                'Save without time'
            )
            if (!ok) return
        }
        await store.save()
    }

    return (
        <section className="savebar">
            <div className="save-summary" data-testid="save-summary">
                {session ? (
                    <>
                        Will save <strong>{entry?.name}</strong>, try {session.number}:{' '}
                        <strong>{session.time_ms === null ? 'no time' : formatTime(session.time_ms)}</strong> ·{' '}
                        {pointsText(session)}
                        {session.capturing && <span className="muted"> — registering stops on save</span>}
                    </>
                ) : (
                    <span className="muted">No open session.</span>
                )}
            </div>
            <button className="btn" disabled={!session || busy} onClick={() => setEnding('RETIRED')}>
                Retire…
            </button>
            <button className="btn btn-danger-outline" disabled={!session || busy} onClick={() => setEnding('ELIMINATED')}>
                Eliminate…
            </button>
            <button className="btn btn-primary btn-large" disabled={!session || busy} onClick={() => void save()}>
                Save &amp; next
            </button>
            {ending && session && (
                <EndRoundDialog
                    ending={ending}
                    name={entry?.name ?? 'this competitor'}
                    summary={`try ${session.number}, ${pointsText(session)} so far`}
                    fenceCount={state.competition.fence_count}
                    onCancel={() => setEnding(null)}
                    onDone={async (fence) => {
                        setEnding(null)
                        await store.endRound(fence ? { outcome: ending, fence } : { outcome: ending })
                    }}
                />
            )}
        </section>
    )
}

/** Confirms a retirement / elimination and asks, optionally, at which fence the round ended. */
function EndRoundDialog({
    ending,
    name,
    summary,
    fenceCount,
    onDone,
    onCancel,
}: {
    ending: Ending
    name: string
    summary: string
    fenceCount: number
    onDone: (fence: string | null) => void
    onCancel: () => void
}) {
    const [fence, setFence] = useState<string | null>(null)
    const words = ENDING[ending]
    useEffect(() => {
        const onKey = (e: KeyboardEvent) => {
            if (e.key === 'Escape') onCancel()
        }
        window.addEventListener('keydown', onKey)
        return () => window.removeEventListener('keydown', onKey)
    }, [onCancel])
    return (
        <div className="modal-backdrop" onMouseDown={(e) => e.target === e.currentTarget && onCancel()}>
            <div className="modal" role="dialog" aria-modal="true" aria-labelledby="end-title">
                <h2 id="end-title">
                    {words.verb} {name}?
                </h2>
                <div className="modal-message">
                    <p>
                        {words.why} The round ({summary}) is saved as <strong>{words.done}</strong>: it keeps its penalties
                        but gets no time and no place.
                    </p>
                    <p id="end-fence">At which fence did it end? (optional)</p>
                    <div className="fence-picker" role="group" aria-labelledby="end-fence">
                        {fenceRows(fenceCount)
                            .flat()
                            .map(String)
                            .map((n) => (
                                <button
                                    key={n}
                                    className="fence-pick"
                                    aria-pressed={fence === n}
                                    onClick={() => setFence(fence === n ? null : n)}
                                >
                                    {n}
                                </button>
                            ))}
                    </div>
                </div>
                <div className="modal-actions">
                    <button
                        autoFocus
                        className={`btn ${ending === 'ELIMINATED' ? 'btn-danger' : 'btn-primary'}`}
                        onClick={() => onDone(fence)}
                    >
                        {words.verb}
                        {fence ? ` at fence ${fence}` : ''}
                    </button>
                    <button className="btn" onClick={onCancel}>
                        Cancel
                    </button>
                </div>
            </div>
        </div>
    )
}
