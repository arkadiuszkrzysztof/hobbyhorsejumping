import { useStore } from '../state/context'

/** Connection lost / session open elsewhere — shown above the work area, not blocking it. */
export function Banners() {
    const [snap, store] = useStore()
    const busy = snap.state?.busy
    return (
        <>
            {snap.connection === 'offline' && (
                <div className="banner banner-offline" role="alert">
                    <strong>Connection to the server lost.</strong> Reconnecting… What you see may be out of
                    date and gate signals are not shown live.
                </div>
            )}
            {busy && (
                <div className="banner banner-busy" role="alert">
                    <span>
                        <strong>A session is open in “{busy.competition_name}”.</strong> Nothing can be
                        registered here until it is saved or discarded there.
                    </span>
                    <button className="btn btn-small" onClick={() => store.open(busy.competition_id)}>
                        Switch to “{busy.competition_name}”
                    </button>
                </div>
            )}
        </>
    )
}

/** Dismissible error toasts; float over the page without blocking it. */
export function ErrorBar() {
    const [snap, store] = useStore()
    if (snap.errors.length === 0) return null
    return (
        <div className="errorbar" aria-live="assertive">
            {snap.errors.map((e) => (
                <div key={e.id} className="error" role="alert">
                    <span>{e.message}</span>
                    <button
                        className="error-close"
                        aria-label="Dismiss error"
                        onClick={() => store.dismissError(e.id)}
                    >
                        ×
                    </button>
                </div>
            ))}
        </div>
    )
}
