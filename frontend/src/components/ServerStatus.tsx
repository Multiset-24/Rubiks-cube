import { useEffect, useState } from 'react';
import { onServerStatus, serverStatus, type ServerStatus as S } from '../lib/api';

const TEXT: Record<S, string> = {
  unknown: 'Connecting…',
  waking: 'Server starting (up to a minute)',
  online: 'Server online',
  offline: 'Server unreachable',
};

export function ServerStatus() {
  const [s, setS] = useState<S>(serverStatus());
  useEffect(() => {
    const unsubscribe = onServerStatus(setS);
    setS(serverStatus()); // the first ping may have finished before we subscribed
    return () => {
      unsubscribe();
    };
  }, []);
  return (
    <span className={`status status--${s}`} title={TEXT[s]} aria-live="polite">
      <span className="status__dot" />
      <span className="status__text">{TEXT[s]}</span>
    </span>
  );
}
