import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { App } from './App'
import { unlockSound } from './sound'
import { CompetitionStore, browserDeps } from './state/store'
import './styles.css'

// Browsers allow sound only after the page has been touched; see sound.ts.
window.addEventListener('pointerdown', unlockSound)
window.addEventListener('keydown', unlockSound)

const store = new CompetitionStore(browserDeps())

createRoot(document.getElementById('root')!).render(
    <StrictMode>
        <App store={store} />
    </StrictMode>
)
