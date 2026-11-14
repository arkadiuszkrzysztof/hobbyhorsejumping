import { useEffect } from 'react'
import { AskProvider } from './components/AskDialog'
import { Banners, ErrorBar } from './components/Banners'
import { CompetitionForm } from './components/CompetitionForm'
import { CompetitorList } from './components/CompetitorList'
import { FenceGrid } from './components/FenceGrid'
import { SaveBar } from './components/SaveBar'
import { SessionHeader } from './components/SessionHeader'
import { SignalColumns } from './components/SignalColumns'
import { TopBar } from './components/TopBar'
import { StoreProvider, useStore } from './state/context'
import type { CompetitionStore } from './state/store'

export function App({ store }: { store: CompetitionStore }) {
    useEffect(() => {
        void store.start()
        return () => store.dispose()
    }, [store])
    return (
        <StoreProvider store={store}>
            <AskProvider>
                <Screen />
                <ErrorBar />
            </AskProvider>
        </StoreProvider>
    )
}

function Screen() {
    const [snap, store] = useStore()

    if (snap.phase === 'loading') return <div className="center-screen muted">Loading…</div>
    if (snap.phase === 'unreachable')
        return (
            <div className="center-screen">
                <h1>Cannot reach the timing server</h1>
                <p className="muted">Check that the backend is running and this device is on the arena WiFi.</p>
                <button className="btn btn-primary btn-large" onClick={() => void store.start()}>
                    Try again
                </button>
            </div>
        )
    if (snap.phase === 'first-run')
        return (
            <div className="center-screen">
                <div className="first-run">
                    <h1>Create the first competition</h1>
                    <CompetitionForm
                        submitLabel="Create competition"
                        busy={snap.pending.has('competition:create')}
                        onSubmit={(competition) => void store.createCompetition(competition)}
                    />
                </div>
            </div>
        )

    return (
        <div className="app">
            <TopBar />
            <Banners />
            {snap.state ? (
                <div className="workspace">
                    <CompetitorList />
                    <main className="main">
                        <SessionHeader />
                        <FenceGrid />
                        <SignalColumns />
                        <SaveBar />
                    </main>
                </div>
            ) : (
                <div className="center-screen muted">Loading competition…</div>
            )}
        </div>
    )
}
