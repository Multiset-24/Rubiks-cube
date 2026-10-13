import { api, type RunInput } from './api';
import { load, save, uuid } from './storage';

// Runs recorded while signed out (or while the server was unreachable) live in
// localStorage with a client id, and are uploaded on sign-in. The client id
// makes the upload idempotent, so a retry can never create duplicates.
export interface LocalRun extends RunInput {
  clientId: string;
  createdAtMs: number;
}

const KEY = 'zen-cube:guest-runs';

export function localRuns(): LocalRun[] {
  return load<LocalRun[]>(KEY, []);
}

function setLocalRuns(runs: LocalRun[]) {
  save(KEY, runs.slice(-200));
}

export async function saveRun(token: string | null, run: RunInput): Promise<'saved' | 'local'> {
  const full: LocalRun = { ...run, clientId: run.clientId ?? uuid(), createdAtMs: run.createdAtMs ?? Date.now() };
  if (token) {
    try {
      await api.createRun(token, full);
      return 'saved';
    } catch {
      /* fall through: keep it locally and sync later */
    }
  }
  setLocalRuns([...localRuns(), full]);
  return 'local';
}

export async function syncGuestRuns(token: string): Promise<number> {
  const runs = localRuns();
  if (!runs.length) return 0;
  try {
    const { imported } = await api.importRuns(token, runs);
    setLocalRuns([]);
    return imported;
  } catch {
    return 0;
  }
}

export function deleteLocalRun(clientId: string) {
  setLocalRuns(localRuns().filter((r) => r.clientId !== clientId));
}
