import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { App } from './App'
import { CompetitionStore, browserDeps } from './state/store'
import './styles.css'

const store = new CompetitionStore(browserDeps())

createRoot(document.getElementById('root')!).render(
    <StrictMode>
        <App store={store} />
    </StrictMode>
)
