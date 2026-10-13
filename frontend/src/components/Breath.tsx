// A slow breathing circle: the loading state, and a small invitation to relax.
export function Breath({ label }: { label?: string }) {
  return (
    <div className="breath" role="status" aria-live="polite">
      <div className="breath__circle" />
      {label && <p className="breath__label">{label}</p>}
    </div>
  );
}
