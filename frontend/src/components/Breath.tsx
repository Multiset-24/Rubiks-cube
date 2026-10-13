// Loading indicator: a slowly pulsing circle.
export function Breath({ label }: { label?: string }) {
  return (
    <div className="breath" role="status" aria-live="polite">
      <div className="breath__circle" />
      {label && <p className="breath__label">{label}</p>}
    </div>
  );
}
