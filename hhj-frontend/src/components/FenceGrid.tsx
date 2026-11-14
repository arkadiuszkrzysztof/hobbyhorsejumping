import { FENCE_PARTS, fenceRows } from '../lib/fences'
import { formatPoints } from '../lib/points'
import { useStore } from '../state/context'

export function FenceGrid() {
    const [snap, store] = useStore()
    const state = snap.state!
    const session = state.session
    const rules = state.competition
    const knocked = new Set(session?.penalties ?? [])
    const refusals = session?.refusals ?? {}
    const rows = fenceRows(rules.fence_count)
    const columns = rows[0]?.length ?? 1
    const disabled = !session

    const knockdown = (label: string, className: string, text: string) => {
        const on = knocked.has(label)
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

    const refusal = (fence: string) => {
        const count = refusals[fence] ?? 0
        return (
            <button
                className={`fence-part fence-refusal${count > 0 ? ' refused' : ''}`}
                aria-label={`Refusal at fence ${fence}${count > 0 ? ` (${count})` : ''}`}
                aria-pressed={count > 0}
                title="Refusal: each click adds one, the third clears them"
                disabled={disabled || snap.pending.has(`refusal:${fence}`)}
                onClick={() => void store.refusal(fence)}
            >
                {count > 1 ? `R${count}` : 'R'}
            </button>
        )
    }

    return (
        <section className={`fences${disabled ? ' fences-disabled' : ''}`} aria-label="Fences — click to mark a fault">
            <h2 className="section-title fences-title">
                <span>Penalties {disabled && <span className="muted">— open a session to mark fences</span>}</span>
                {session && (
                    <span className="points-now" data-testid="points-now">
                        {formatPoints(session.fence_points)} at fences
                        {session.time_points > 0 && <> + {formatPoints(session.time_points)} time</>} ={' '}
                        <strong>{formatPoints(session.points)} pts</strong>
                    </span>
                )}
            </h2>
            {rows.map((row, i) => (
                <div key={i} className="fence-row" style={{ gridTemplateColumns: `repeat(${columns}, minmax(0, 1fr))` }}>
                    {row.map((n) => {
                        const fence = String(n)
                        const hit =
                            (refusals[fence] ?? 0) > 0 || [fence, ...FENCE_PARTS.map((p) => `${n}${p}`)].some((l) => knocked.has(l))
                        return (
                            <div key={n} className={`fence${hit ? ' fence-hit' : ''}`}>
                                {knockdown(fence, 'fence-main', fence)}
                                <div className="fence-parts">
                                    {FENCE_PARTS.map((p) => (
                                        <span key={p}>{knockdown(`${n}${p}`, 'fence-part', p)}</span>
                                    ))}
                                    <span>{refusal(fence)}</span>
                                </div>
                            </div>
                        )
                    })}
                </div>
            ))}
            <p className="fences-legend">
                Number or A / B / C: knocked down ({formatPoints(rules.knockdown_points)} pts) · R: refusal (
                {formatPoints(rules.refusal_points)} pts)
                {rules.time_allowed !== null && (
                    <>
                        {' '}
                        · time allowed {rules.time_allowed} s, then {formatPoints(rules.overtime_points)} pts per full second
                    </>
                )}
            </p>
        </section>
    )
}
