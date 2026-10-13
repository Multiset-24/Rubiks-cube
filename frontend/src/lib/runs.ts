import { api, ApiError, type RunInput } from './api';
import { load, save, uuid } from './storage';

// Runs recorded while signed out (or while the server was unreachable) live in
// localStorage with a client id, and are uploaded on sign-in. The client id
// makes the upload idempotent, so a retry can never create duplicates.
export interface LocalRun extends RunInput {
  clientId: string;
  createdAtMs: number;
}

const KEY = 'zen-cube:guest-runs';
const DAY_MS = 86_400_000; // the server's limit for durationMs

export function localRuns(): LocalRun[] {
  return load<LocalRun[]>(KEY, []);
}

function setLocalRuns(runs: LocalRun[]) {
  save(KEY, runs.slice(-200));
}

// A tutor tab left open overnight would otherwise be rejected.
function clamp(run: LocalRun): LocalRun {
  if (run.durationMs === undefined) return run;
  return { ...run, durationMs: Math.max(0, Math.min(DAY_MS, Math.round(run.durationMs))) };
}

// The server rejected the run itself, so it would fail again on every sync.
// Anything else (no connection, a server error, an expired session, a rate
// limit) is worth keeping for later.
function invalid(e: unknown): boolean {
  return e instanceof ApiError && e.status === 400;
}

// Uploads one stored run. If the server refused only its time (a device clock
// that runs fast), it is sent again without one and the server stamps it.
async function upload(token: string, run: LocalRun) {
  try {
    await api.createRun(token, run);
  } catch (e) {
    if (!invalid(e)) throw e;
    const fields = ((e as ApiError).details as { fields?: object } | undefined)?.fields ?? {};
    if (Object.keys(fields).join() !== 'createdAtMs') throw e;
    await api.createRun(token, { ...run, createdAtMs: undefined });
  }
}

export async function saveRun(token: string | null, run: RunInput): Promise<'saved' | 'local' | 'rejected'> {
  const full = clamp({ ...run, clientId: run.clientId ?? uuid(), createdAtMs: run.createdAtMs ?? Date.now() });
  if (token) {
    try {
      // The server stamps the time itself, so a device clock that runs fast
      // can't get the run rejected.
      await api.createRun(token, { ...full, createdAtMs: run.createdAtMs });
      return 'saved';
    } catch (e) {
      if (invalid(e)) return 'rejected';
    }
  }
  setLocalRuns([...localRuns(), full]);
  return 'local';
}

export async function syncGuestRuns(token: string): Promise<number> {
  const runs = localRuns().map(clamp);
  if (!runs.length) return 0;
  const handled = new Set<string>();
  let imported = 0;
  try {
    imported = (await api.importRuns(token, runs)).imported;
    runs.forEach((r) => handled.add(r.clientId));
  } catch (e) {
    if (!invalid(e)) return 0;
    // The batch is all or nothing, so one invalid run fails it. Send the runs
    // one at a time and drop only the ones the server rejects.
    for (const r of runs) {
      try {
        await upload(token, r);
        imported++;
      } catch (err) {
        if (!invalid(err)) break;
      }
      handled.add(r.clientId);
    }
  }
  // Runs saved while this was in flight stay for the next sync.
  setLocalRuns(localRuns().filter((r) => !handled.has(r.clientId)));
  return imported;
}

export function deleteLocalRun(clientId: string) {
  setLocalRuns(localRuns().filter((r) => r.clientId !== clientId));
}
