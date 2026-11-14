/** A hobby horse's name with its marker. Renders nothing when the competitor gave none. */
export function HorseName({ name, className }: { name: string; className: string }) {
    if (!name) return null
    return (
        <span className={className}>
            <span aria-hidden="true">🐴 </span>
            <span className="sr-only">Horse: </span>
            {name}
        </span>
    )
}
