// The start bell. It is synthesized, so there is no sound file to ship and it
// works without internet.

/** One strike: [frequency in Hz, level, seconds until it has faded by 60 dB]. Slightly out of tune, like a real bell. */
const PARTIALS: ReadonlyArray<readonly [number, number, number]> = [
    [1174.7, 0.5, 2.2],
    [2349.3, 0.22, 1.4],
    [3218.6, 0.16, 0.9],
    [6308.0, 0.07, 0.4],
]
/** Two strikes ("ding-ding"), seconds after the start. */
const STRIKES: readonly number[] = [0, 0.32]
/** Keeps the sum of both strikes below full scale (rendered offline: peak 0.92). */
const MASTER = 0.9
const ATTACK_S = 0.004

/** Schedule the bell on an audio context, starting at `at` (in the context's time). */
export function scheduleBell(ctx: BaseAudioContext, at: number): void {
    const out = ctx.createGain()
    out.gain.value = MASTER
    out.connect(ctx.destination)
    let last: OscillatorNode | null = null
    let lastEnd = 0
    for (const offset of STRIKES) {
        for (const [frequency, level, fade] of PARTIALS) {
            const t = at + offset
            const end = t + ATTACK_S + fade
            const osc = ctx.createOscillator()
            const envelope = ctx.createGain()
            osc.frequency.value = frequency
            envelope.gain.setValueAtTime(0, t)
            envelope.gain.linearRampToValueAtTime(level, t + ATTACK_S)
            envelope.gain.exponentialRampToValueAtTime(level / 1000, end)
            osc.connect(envelope)
            envelope.connect(out)
            osc.start(t)
            osc.stop(end)
            if (end > lastEnd) {
                lastEnd = end
                last = osc
            }
        }
    }
    // When the partial that sounds longest has ended, let go of the nodes.
    if (last) last.onended = () => out.disconnect()
}

let context: AudioContext | null = null

function audio(): AudioContext | null {
    if (context) return context
    const Ctor =
        window.AudioContext ?? (window as unknown as { webkitAudioContext?: typeof AudioContext }).webkitAudioContext
    if (!Ctor) return null
    context = new Ctor()
    return context
}

/**
 * Browsers let a page make sound only after the user has touched it. Called on
 * every tap and key press, this switches the audio on at the first one, so
 * that a bell triggered later by the server — somebody started the session on
 * another screen — is heard here too.
 */
export function unlockSound(): void {
    const ctx = audio()
    if (ctx && ctx.state === 'suspended') void ctx.resume().catch(() => undefined)
}

/** Ring the bell now. Silent when the browser has no audio or does not allow it yet. */
export function ringBell(): void {
    const ctx = audio()
    if (!ctx) return
    if (ctx.state === 'running') {
        scheduleBell(ctx, ctx.currentTime + 0.02)
        return
    }
    // Not switched on yet. Ring only if the browser allows it right away: a
    // bell that sounds seconds later, at the next tap, would be a false signal.
    const asked = Date.now()
    void ctx
        .resume()
        .then(() => {
            if (Date.now() - asked < 1000) scheduleBell(ctx, ctx.currentTime + 0.02)
        })
        .catch(() => undefined)
}
