import type { FinishSignal, StartSignal } from '../api/types'
import { formatTime, formatTimeOfDay } from '../lib/time'
import { useStore } from '../state/context'

type Kind = 'start' | 'finish'

export function SignalColumns() {
    const [snap, store] = useStore()
    const session = snap.state!.session
    const pending = snap.pending.has('pick')

    if (!session) {
        return (
            <section className="signals signals-empty-session">
                <p className="muted">Gate signals of the session appear here once registering starts.</p>
            </section>
        )
    }

    const column = (kind: Kind, list: (StartSignal | FinishSignal)[]) => {
        const countingId = kind === 'start' ? session.start_id : session.finish_id
        const auto = kind === 'start' ? session.start_auto : session.finish_auto
        const title = kind === 'start' ? 'START' : 'FINISH'
        const click = (id: string) => {
            if (id === countingId) {
                if (auto) return
                void store.pick(kind === 'start' ? { start: null } : { finish: null })
            } else {
                void store.pick(kind === 'start' ? { start: id } : { finish: id })
            }
        }
        return (
            <div className={`signal-col signal-col-${kind}`} aria-label={`${title} signals`} role="group">
                <h3>{title} signals</h3>
                {list.length === 0 ? (
                    <p className="signal-wait">
                        {kind === 'start'
                            ? session.capturing
                                ? 'Waiting for the start gate…'
                                : 'No START yet — start registering, then the rider crosses the start gate.'
                            : session.start_id
                              ? 'Waiting for the finish gate…'
                              : 'No FINISH yet — waiting for a START first.'}
                    </p>
                ) : (
                    <ul>
                        {list.map((sig) => {
                            const counting = sig.id === countingId
                            const cls = ['signal', counting && 'signal-counting', !sig.in_session && 'signal-outside']
                            return (
                                <li key={sig.id}>
                                    <button
                                        className={cls.filter(Boolean).join(' ')}
                                        aria-pressed={counting}
                                        disabled={pending}
                                        title={
                                            counting
                                                ? auto
                                                    ? 'Counts (automatic choice)'
                                                    : 'Counts (picked) — click to go back to automatic'
                                                : 'Click to make this one count'
                                        }
                                        onClick={() => click(sig.id)}
                                    >
                                        <span className="signal-time">{formatTimeOfDay(sig.sensor_time)}</span>
                                        {'time_ms' in sig && <span className="signal-result">{formatTime(sig.time_ms)}</span>}
                                        {!sig.in_session && <span className="tag tag-outside">outside session</span>}
                                        {counting && <span className="tag tag-counting">{auto ? 'auto' : 'picked'}</span>}
                                    </button>
                                </li>
                            )
                        })}
                    </ul>
                )}
            </div>
        )
    }

    return (
        <section className="signals" aria-label="Gate signals">
            {column('start', session.starts)}
            {column('finish', session.finishes)}
        </section>
    )
}
