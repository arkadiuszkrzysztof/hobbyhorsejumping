# hhj-frontend

Operator console for hobby-horse jumping timing. Vite + React + TypeScript; talks to
`hhj-backend` per `../hhj-backend/API.md`.

```sh
npm install
npm start          # = npm run dev; Vite on :3000, reachable from the LAN (tablet)
npm test           # vitest run
npm run build      # type-check + production build into dist/
npm run preview    # serve dist/ on :3000 with the same proxy
```

The app only uses relative URLs (`/api/...`, `/ws/live/<id>/`). The dev and preview servers
proxy them to the backend at `VITE_BACKEND` (default `http://127.0.0.1:3003`), e.g.
`VITE_BACKEND=http://192.168.1.10:3003 npm start`. A production deployment must route
`/api` and `/ws` to the backend the same way.

Layout of `src/`: `api/` (contract types + HTTP client), `state/` (the competition store:
fetch, WebSocket with reconnect, actions, when the start bell rings), `lib/` (pure time /
fence / ranking helpers), `sound.ts` (the bell itself, synthesized with Web Audio),
`components/`, `test/` (fakes).
