import { FENCE_PARTS, fenceRows } from '../lib/fences'
import { useStore } from '../state/context'

export function FenceGrid() {
    const [snap, store] = useStore()
    const state = snap.state!
    const session = state.session
    const penalties = new Set(session?.penalties ?? [])
    const rows = fenceRows(state.competition.fence_count)
    const columns = rows[0]?.length ?? 1
    const disabled = !session

    const button = (label: string, className: string, text: string) => {
        const on = penalties.has(label)
        return (
            <button
                className={`${className}${on ? ' penalised' : ''}`}
                aria-label={`Fence ${label}`}
                aria-pressed={on}
                disabled={disabled || snap.pending.has(`penalty:${label}`)}
                onClick={() => void store.penalty(label)}
            >
                {text}
            </button>
        )
    }

    return (
        <section className={`fences${disabled ? ' fences-disabled' : ''}`} aria-label="Fences — click to toggle a penalty">
            <h2 className="section-title">
                Penalties {disabled && <span className="muted">— open a session to mark fences</span>}
            </h2>
            {rows.map((row, i) => (
                <div key={i} className="fence-row" style={{ gridTemplateColumns: `repeat(${columns}, minmax(0, 1fr))` }}>
                    {row.map((n) => {
                        const any = [String(n), ...FENCE_PARTS.map((p) => `${n}${p}`)].some((l) => penalties.has(l))
                        return (
                            <div key={n} className={`fence${any ? ' fence-hit' : ''}`}>
                                {button(String(n), 'fence-main', String(n))}
                                <div className="fence-parts">
                                    {FENCE_PARTS.map((p) => (
                                        <span key={p}>{button(`${n}${p}`, 'fence-part', p)}</span>
                                    ))}
                                </div>
                            </div>
                        )
                    })}
                </div>
            ))}
        </section>
    )
}
