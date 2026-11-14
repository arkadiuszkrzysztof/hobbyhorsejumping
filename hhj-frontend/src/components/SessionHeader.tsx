import { useEffect, useState } from 'react'
import type { Session } from '../api/types'
import { computeReadout, estimateServerNow, formatTime } from '../lib/time'
import { useStore } from '../state/context'
import { useConfirm } from './AskDialog'
import { HorseName } from './HorseName'

type Status = 'IDLE' | 'REGISTERING' | 'PAUSED'

export function sessionStatus(session: Session | null): Status {
    if (!session) return 'IDLE'
    return session.capturing ? 'REGISTERING' : 'PAUSED'
}

/** Big readout mirroring the external display; runs on requestAnimationFrame while the clock is live. */
export function TimeReadout({ session, now = Date.now }: { session: Session | null; now?: () => number }) {
    const [snap] = useStore()
    const offset = snap.clockOffset
    const initial = computeReadout(session, estimateServerNow(offset, now()))
    const [text, setText] = useState(formatTime(initial.ms))

    useEffect(() => {
        const first = computeReadout(session, estimateServerNow(offset, now()))
        setText(formatTime(first.ms))
        if (first.mode !== 'running') return
        let raf = 0
        const tick = () => {
            setText(formatTime(computeReadout(session, estimateServerNow(offset, now())).ms))
            raf = requestAnimationFrame(tick)
        }
        raf = requestAnimationFrame(tick)
        return () => cancelAnimationFrame(raf)
    }, [session, offset, now])

    return (
        <div className={`readout readout-${initial.mode}`} data-testid="readout" aria-label="Time">
            {text}
        </div>
    )
}

export function SessionHeader() {
    const [snap, store] = useStore()
    const confirm = useConfirm()
    const state = snap.state!
    const session = state.session
    const entryId = session?.entry_id ?? state.selected_entry
    const entry = state.entries.find((e) => e.id === entryId) ?? null
    const status = sessionStatus(session)
    const nextTry = entry ? Math.max(0, ...entry.runs.map((r) => r.number)) + 1 : null
    const capturing = session?.capturing ?? false
    const canCapture = !state.busy && (session !== null || state.selected_entry !== null)

    const discard = async () => {
        if (!session || !entry) return
        const ok = await confirm(
            `Discard try ${session.number} of ${entry.name}?`,
            <p>Penalties and the chosen signals are thrown away. The gate signals stay stored but belong to no run.</p>,
            'Discard session',
            'danger'
        )
        if (ok) await store.discard()
    }

    return (
        <section className={`session-header status-${status.toLowerCase()}`}>
            <div className="current">
                <div className="current-label">Current competitor</div>
                <div className="current-name" data-testid="current-name">
                    {entry ? entry.name : <span className="muted">Nobody selected — pick a competitor on the left</span>}
                </div>
                {entry && <HorseName name={entry.horse} className="current-horse" />}
                <div className="current-meta">
                    {entry && <span className="try">{session ? `Try ${session.number}` : `Next: try ${nextTry}`}</span>}
                    <span className={`badge badge-${status.toLowerCase()}`} data-testid="status-badge">
                        {status === 'REGISTERING' && <span className="live-dot" aria-hidden="true" />}
                        {status}
                    </span>
                </div>
            </div>
            <TimeReadout session={session} />
            <div className="actions">
                <button
                    className={`btn btn-huge ${capturing ? 'btn-stop' : 'btn-go'}`}
                    disabled={!canCapture || snap.pending.has('capture')}
                    onClick={() => void store.capture(!capturing)}
                >
                    {capturing ? '■ Stop registering' : session ? '● Resume registering' : '● Start registering'}
                </button>
                <button
                    className="btn btn-danger-outline"
                    disabled={!session || snap.pending.has('discard')}
                    onClick={() => void discard()}
                >
                    Discard session
                </button>
            </div>
        </section>
    )
}
