/**
 * Where to scroll a list so that one row sits in the middle of what is
 * visible of it — the part not covered by a bar stuck to its bottom.
 * All values in px; the result is a valid scrollTop.
 */
export function centredScrollTop(row: { top: number; height: number }, view: { height: number; coveredBottom: number; scrollHeight: number }): number {
    const visible = Math.max(view.height - view.coveredBottom, 0)
    const wanted = row.top - (visible - row.height) / 2
    return Math.max(0, Math.min(wanted, view.scrollHeight - view.height))
}
