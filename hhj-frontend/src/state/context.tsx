import { createContext, useContext, useSyncExternalStore, type ReactNode } from 'react'
import type { CompetitionStore, Snapshot } from './store'

const StoreContext = createContext<CompetitionStore | null>(null)

export function StoreProvider({ store, children }: { store: CompetitionStore; children: ReactNode }) {
    return <StoreContext.Provider value={store}>{children}</StoreContext.Provider>
}

export function useStore(): [Snapshot, CompetitionStore] {
    const store = useContext(StoreContext)
    if (!store) throw new Error('useStore outside StoreProvider')
    const snap = useSyncExternalStore(store.subscribe, store.getSnapshot)
    return [snap, store]
}
