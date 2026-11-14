import { createContext, useCallback, useContext, useEffect, useRef, useState, type ReactNode } from 'react'

export interface AskAction {
    label: string
    value: string
    tone?: 'primary' | 'danger'
}

export interface AskOptions {
    title: string
    message?: ReactNode
    actions: AskAction[]
    cancelLabel?: string
}

type Ask = (opts: AskOptions) => Promise<string | null>

const AskContext = createContext<Ask | null>(null)

interface Open extends AskOptions {
    resolve: (v: string | null) => void
}

/** Promise-based in-page dialog: `await ask({...})` → chosen action value, or null on cancel. */
export function AskProvider({ children }: { children: ReactNode }) {
    const [open, setOpen] = useState<Open | null>(null)
    const ask = useCallback<Ask>(
        (opts) =>
            new Promise((resolve) => {
                setOpen((prev) => {
                    prev?.resolve(null)
                    return { ...opts, resolve }
                })
            }),
        []
    )
    const close = (value: string | null) => {
        open?.resolve(value)
        setOpen(null)
    }
    return (
        <AskContext.Provider value={ask}>
            {children}
            {open && <AskModal opts={open} onClose={close} />}
        </AskContext.Provider>
    )
}

function AskModal({ opts, onClose }: { opts: AskOptions; onClose: (v: string | null) => void }) {
    const firstRef = useRef<HTMLButtonElement>(null)
    useEffect(() => {
        firstRef.current?.focus()
        const onKey = (e: KeyboardEvent) => {
            if (e.key === 'Escape') onClose(null)
        }
        window.addEventListener('keydown', onKey)
        return () => window.removeEventListener('keydown', onKey)
    }, [onClose])
    return (
        <div className="modal-backdrop" onMouseDown={(e) => e.target === e.currentTarget && onClose(null)}>
            <div className="modal" role="dialog" aria-modal="true" aria-labelledby="ask-title">
                <h2 id="ask-title">{opts.title}</h2>
                {opts.message && <div className="modal-message">{opts.message}</div>}
                <div className="modal-actions">
                    {opts.actions.map((a, i) => (
                        <button
                            key={a.value}
                            ref={i === 0 ? firstRef : undefined}
                            className={`btn ${a.tone === 'danger' ? 'btn-danger' : 'btn-primary'}`}
                            onClick={() => onClose(a.value)}
                        >
                            {a.label}
                        </button>
                    ))}
                    <button className="btn" onClick={() => onClose(null)}>
                        {opts.cancelLabel ?? 'Cancel'}
                    </button>
                </div>
            </div>
        </div>
    )
}

export function useAsk(): Ask {
    const ask = useContext(AskContext)
    if (!ask) throw new Error('useAsk outside AskProvider')
    return ask
}

/** Yes/no shortcut. */
export function useConfirm() {
    const ask = useAsk()
    return useCallback(
        async (title: string, message: ReactNode, okLabel: string, tone: AskAction['tone'] = 'primary') =>
            (await ask({ title, message, actions: [{ label: okLabel, value: 'ok', tone }] })) === 'ok',
        [ask]
    )
}
