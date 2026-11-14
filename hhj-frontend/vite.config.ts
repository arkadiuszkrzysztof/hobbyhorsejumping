import { loadEnv } from 'vite'
import { defineConfig } from 'vitest/config'
import react from '@vitejs/plugin-react'

// The app only uses relative URLs (/api/..., /ws/...); the dev and preview
// servers forward them to the Django backend, so the bundle never contains a
// backend address and there is no CORS.
export default defineConfig(({ mode }) => {
    const env = loadEnv(mode, '.', '')
    const target = env.VITE_BACKEND || 'http://127.0.0.1:3003'
    const proxy = {
        '/api': { target, changeOrigin: true },
        '/ws': { target, changeOrigin: true, ws: true },
    }
    return {
        plugins: [react()],
        server: { port: 3000, strictPort: true, host: true, proxy },
        preview: { port: 3000, strictPort: true, host: true, proxy },
        test: {
            environment: 'jsdom',
            setupFiles: ['./src/test/setup.ts'],
            restoreMocks: true,
        },
    }
})
