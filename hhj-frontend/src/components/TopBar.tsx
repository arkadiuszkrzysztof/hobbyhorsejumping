import { useState } from 'react'
import type { Device, DeviceRole } from '../api/types'
import { useStore } from '../state/context'
import type { Connection } from '../state/store'
import { CompetitionForm } from './CompetitionForm'

const ROLES: DeviceRole[] = ['START', 'FINISH', 'TIMER']
const ROLE_LABEL: Record<DeviceRole, string> = { START: 'Start gate', FINISH: 'Finish gate', TIMER: 'Timer' }

export function deviceLight(d: Device | undefined): { tone: 'ok' | 'warn' | 'off'; text: string } {
    if (!d || !d.online) return { tone: 'off', text: 'offline' }
    if (d.ok === false) return { tone: 'warn', text: 'online, beam blocked / not aligned' }
    return { tone: 'ok', text: 'online' }
}

const CONNECTION_TEXT: Record<Connection, string> = {
    online: 'Live',
    connecting: 'Connecting…',
    offline: 'Offline',
}

export function TopBar() {
    const [snap, store] = useStore()
    const [form, setForm] = useState<'new' | 'edit' | null>(null)
    const current = snap.state?.competition
    const devices = snap.state?.devices ?? []

    return (
        <header className="topbar">
            <div className="topbar-row">
                <span className="brand">HHJ</span>
                <label className="picker">
                    <span className="sr-only">Competition</span>
                    <select
                        value={snap.competitionId ?? ''}
                        onChange={(e) => {
                            setForm(null)
                            store.open(e.target.value)
                        }}
                        onFocus={() => void store.refreshCompetitions()}
                    >
                        {snap.competitions.map((c) => (
                            <option key={c.id} value={c.id}>
                                {c.name}
                            </option>
                        ))}
                    </select>
                </label>
                <button
                    className="btn btn-small"
                    disabled={!current}
                    aria-expanded={form === 'edit'}
                    onClick={() => setForm(form === 'edit' ? null : 'edit')}
                >
                    Edit
                </button>
                <button
                    className="btn btn-small"
                    aria-expanded={form === 'new'}
                    onClick={() => setForm(form === 'new' ? null : 'new')}
                >
                    + New competition
                </button>
                {current && <span className="fence-count-note">{current.fence_count} fences</span>}

                <div className="topbar-status">
                    <button
                        className="btn btn-small bell-toggle"
                        aria-pressed={snap.bell}
                        title="Ring a bell on this device when registering starts. Switching it on rings it once."
                        onClick={() => store.setBell(!snap.bell)}
                    >
                        {snap.bell ? '🔔 Bell on' : '🔕 Bell off'}
                    </button>
                    {ROLES.map((role) => {
                        const light = deviceLight(devices.find((d) => d.role === role))
                        return (
                            <span
                                key={role}
                                className={`device device-${light.tone}`}
                                title={`${ROLE_LABEL[role]}: ${light.text}`}
                                data-testid={`device-${role}`}
                            >
                                <span className="dot" aria-hidden="true" />
                                {role}
                                <span className="sr-only">: {light.text}</span>
                            </span>
                        )
                    })}
                    <span className={`connection connection-${snap.connection}`} role="status">
                        <span className="dot" aria-hidden="true" />
                        {CONNECTION_TEXT[snap.connection]}
                    </span>
                </div>
            </div>
            {form === 'new' && (
                <div className="topbar-form">
                    <strong>New competition</strong>
                    <CompetitionForm
                        submitLabel="Create"
                        busy={snap.pending.has('competition:create')}
                        onCancel={() => setForm(null)}
                        onSubmit={async (name, fences) => {
                            const r = await store.createCompetition(name, fences)
                            if (r.ok) setForm(null)
                        }}
                    />
                </div>
            )}
            {form === 'edit' && current && (
                <div className="topbar-form">
                    <strong>Edit competition</strong>
                    <CompetitionForm
                        key={current.id}
                        initialName={current.name}
                        initialFences={current.fence_count}
                        submitLabel="Save"
                        busy={snap.pending.has('competition:update')}
                        onCancel={() => setForm(null)}
                        onSubmit={async (name, fence_count) => {
                            const r = await store.updateCompetition({ name, fence_count })
                            if (r.ok) setForm(null)
                        }}
                    />
                </div>
            )}
        </header>
    )
}
