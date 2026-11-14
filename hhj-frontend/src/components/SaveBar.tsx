import { sortPenalties } from '../lib/fences'
import { formatTime } from '../lib/time'
import { useStore } from '../state/context'
import { useConfirm } from './AskDialog'

export function SaveBar() {
    const [snap, store] = useStore()
    const confirm = useConfirm()
    const state = snap.state!
    const session = state.session
    const entry = state.entries.find((e) => e.id === session?.entry_id)
    const pens = sortPenalties(session?.penalties ?? [])
    const penText =
        pens.length === 0 ? 'no penalties' : `${pens.length} ${pens.length === 1 ? 'penalty' : 'penalties'} (${pens.join(', ')})`

    const save = async () => {
        if (!session) return
        if (session.time_ms === null) {
            const ok = await confirm(
                'Save without a time?',
                <p>
                    No START/FINISH pair counts for {entry?.name ?? 'this competitor'}. The run is saved with no time
                    and {penText}.
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
                        <strong>{session.time_ms === null ? 'no time' : formatTime(session.time_ms)}</strong> · {penText}
                        {session.capturing && <span className="muted"> — registering stops on save</span>}
                    </>
                ) : (
                    <span className="muted">No open session.</span>
                )}
            </div>
            <button className="btn btn-primary btn-large" disabled={!session || snap.pending.has('save')} onClick={() => void save()}>
                Save &amp; next
            </button>
        </section>
    )
}
