# API

Base URL in production: `https://zen-cube-solver-api.onrender.com`. Locally: `http://localhost:8080`.

All bodies are JSON. Authenticated routes need `Authorization: Bearer <token>`. Every response carries an `X-Request-Id` header. You can send your own value, or one is generated.

## Errors

Every error uses the same shape:

```json
{
  "error": {
    "code": "INVALID_CUBE",
    "message": "The corners are twisted in a way turning cannot produce.",
    "details": { "issues": [ ... ] },
    "requestId": "9f2c1d4e7a0b3c55"
  }
}
```

| Status | Codes |
| --- | --- |
| 400 | `BAD_JSON`, `BAD_REQUEST`, `UNKNOWN_METHOD`, `VALIDATION_FAILED` (with `details.fields`), `BAD_CURSOR` |
| 401 | `UNAUTHENTICATED`, `INVALID_CREDENTIALS` |
| 404 | `NOT_FOUND` |
| 409 | `EMAIL_TAKEN`, `RUN_LIMIT_REACHED` |
| 413 | `PAYLOAD_TOO_LARGE`, `TOO_MANY_RUNS` |
| 422 | `INVALID_CUBE` (with `details.issues`) |
| 429 | `RATE_LIMITED` (+ `Retry-After`) |
| 503 | `BUSY`, `WARMING_UP`, `STORAGE_UNAVAILABLE`, `SOLVER_UNAVAILABLE` (+ `Retry-After`) |
| 504 | `SOLVER_TIMEOUT` |

## Cube format

A cube is 54 characters: faces in the order U R F D L B, each read row by row while looking at that face, laid out as the usual net (U above F, D below). Each character is the face whose centre has that colour, so a solved cube is:

```
UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB
```

You can also send any six distinct characters, for example `W Y R O G B`. The centres decide which colour belongs to which face. `colorNames` (optional) maps faces to colour names, which the tutor uses in its text:

```json
{ "U": "white", "R": "red", "F": "green", "D": "yellow", "L": "orange", "B": "blue" }
```

## Health

| Route | Purpose |
| --- | --- |
| `GET /healthz` | Liveness: `{"status":"ok"}` |
| `GET /readyz` | Readiness: 200 `{"status":"ready","storage":{"kind":"postgres","available":true}}` once the solver is loaded; 503 while warming up |
| `GET /metrics` | Prometheus metrics (needs `Authorization: Bearer <METRICS_TOKEN>` if that is set) |
| `GET /api/v1/info` | Version, storage kind, available solvers, limits |

## Solving

### `POST /api/v1/validate`

```json
{ "cube": "UUFUUFUUFRRRRRRRRRFFDFFDFFDDDBDDBDDBLLLLLLLLLUBBUBBUBB" }
```

```json
{ "valid": true, "issues": [], "facelets": "UUFUUF..." }
```

When `valid` is false, `issues` lists what is wrong:

```json
{
  "valid": false,
  "issues": [{
    "code": "COLOR_COUNT",
    "message": "There are 10 white stickers; a cube has exactly 9.",
    "hint": "One of the highlighted white stickers is probably another colour (often red/orange or white/yellow mix-ups).",
    "facelets": [0, 1, 2, 3, 5, 6, 7, 8, 4, 28]
  }]
}
```

The issue codes are `LENGTH`, `CENTERS`, `UNKNOWN_COLOR`, `COLOR_COUNT`, `INVALID_CORNER`, `DUPLICATE_CORNER`, `INVALID_EDGE`, `DUPLICATE_EDGE`, `MIRRORED_CORNER`, `TWISTED_CORNER`, `FLIPPED_EDGE` and `PARITY`.

### `POST /api/v1/solve`

```json
{ "cube": "...", "method": "beginner", "colorNames": { "U": "white", ... } }
```

`method` is `beginner` (default), `kociemba` or `optimal`. The response:

```json
{
  "method": "beginner",
  "solver": "layer-by-layer",
  "facelets": "...",
  "moves": "z2 R2 U R ...",
  "moveList": ["z2", "R2", "U", "R", "..."],
  "moveCount": 158,
  "optimal": false,
  "cached": false,
  "elapsedMs": 1.4,
  "stages": [
    {
      "id": "white-corners",
      "name": "First layer corners",
      "goal": "Fill the four bottom corners so ...",
      "recognize": "A white corner is either in the top layer ...",
      "algorithms": [{ "name": "Sexy move", "notation": "R U R' U'" }],
      "steps": [
        {
          "title": "Insert the white-red-green corner",
          "observe": "Its home is the bottom corner between the red and green centres. ...",
          "instruction": "Turn the top until the corner is above its home. ... repeat R U R' U' until white faces down: 3 times.",
          "tip": "White pointing right needs 1 repeat, towards you needs 5, and up needs 3. ...",
          "setup": ["U", "y'"],
          "algorithm": { "name": "Sexy move", "notation": "R U R' U'", "repeat": 3 },
          "moves": ["U", "y'", "R", "U", "R'", "U'", "..."],
          "focus": [8, 9, 20],
          "quiz": {
            "question": "The white sticker points up. How many times will you repeat R U R' U'?",
            "options": ["1", "3", "5"],
            "answer": 1,
            "explanation": "Each R U R' U' twists the corner by one step: ..."
          }
        }
      ]
    }
  ]
}
```

Notes:

- `moveCount` counts face turns only. Whole-cube rotations (`x`, `y`, `z`) appear in the move lists, because the tutor uses them, but don't count.
- `focus` lists the sticker indices of the piece the step is about, taken in the state at the start of that step.
- Stages that are already done have no steps and a `skippedNote`.
- `kociemba` and `optimal` return one stage with one step per move, so clients can use the same player.
- `optimal` may answer with `"method": "kociemba", "fallbackFrom": "optimal", "fallbackReason": "..."` when the cube is too far from solved for an optimal search within the time limit.
- `X-Cache` is `HIT`, `MISS` or `COALESCED` (another identical request computed it).

### `GET /api/v1/solve?cube=...&method=...`

Returns the same body, with `Cache-Control: public, max-age=31536000, immutable`, since the answer depends only on the query.

### `GET /api/v1/scramble`

A random-state scramble: a uniformly random cube, solved with Kociemba, with the solution inverted.

```json
{ "scramble": "R2 U' R2 U2 L2 ...", "moveList": ["R2", "U'", "..."], "facelets": "..." }
```

## Accounts

### `POST /api/v1/auth/register`

```json
{ "email": "you@example.com", "password": "at least 8 chars", "displayName": "Sourav" }
```

Returns `201 { "token": "...", "user": { "id": 1, "email": "you@example.com", "displayName": "Sourav", "createdAt": "..." } }`. Email is trimmed and lower-cased. Field errors come back as `400 VALIDATION_FAILED` with `details.fields`, and a duplicate email as `409 EMAIL_TAKEN`.

### `POST /api/v1/auth/login`

`{ "email", "password" }` returns the same shape as register. Wrong password and unknown email both return `401 INVALID_CREDENTIALS`.

### `GET /api/v1/me`

`{ "user": { ... } }`

## Runs

A run is one tutor session, followed solution or timed solve.

### `POST /api/v1/runs`

```json
{
  "kind": "timer",
  "durationMs": 38770,
  "completed": true,
  "scramble": "R U F' ...",
  "clientId": "5f0c9a7e-..."
}
```

| Field | Type / rule |
| --- | --- |
| `kind` | `tutor`, `fast`, `optimal` or `timer` (required) |
| `method` | string, max 32 |
| `facelets` | empty or 54 chars |
| `scramble` | string, max 200 |
| `moveCount` | 0–10000 |
| `durationMs` | 0–86,400,000 |
| `completed` | boolean |
| `quizCorrect`, `quizTotal` | 0–1000, correct ≤ total |
| `stepsCompleted`, `totalSteps` | 0–1000 |
| `clientId` | string, max 64. Idempotency key: repeating a request with the same `clientId` returns the stored run instead of creating another |
| `createdAtMs` | optional, within the last 400 days. Used when uploading runs recorded offline |

Returns `201` with the stored run (`id`, all fields, `createdAt`).

### `POST /api/v1/runs/batch`

`{ "runs": [ ... ] }`, up to 200 runs. All runs are validated before any is written. Returns `201 { "runs": [...], "imported": n }`.

### `GET /api/v1/runs?limit=20&cursor=...`

Newest first, `limit` between 1 and 100. Returns `{ "runs": [...], "nextCursor": "..." | null }`. Pass `nextCursor` back to get the next page.

### `DELETE /api/v1/runs/{id}`

`204`, or `404` if the run doesn't exist or isn't yours.

### `GET /api/v1/stats`

```json
{
  "totalRuns": 11,
  "completedRuns": 11,
  "byKind": { "timer": { "total": 10, "completed": 10 }, "tutor": { "total": 1, "completed": 1 } },
  "movesFollowed": 160,
  "quiz": { "correct": 7, "total": 9, "accuracy": 0.78 },
  "timer": { "bestMs": 33410, "ao5Ms": 37460, "ao12Ms": null, "recentMs": [33410, 38770, "..."] },
  "streak": { "current": 7, "longest": 7 },
  "activeDays": ["2026-10-13", "2026-10-12", "..."]
}
```

Ao5/Ao12 follow the WCA rule: drop the best and worst of the last 5 (or 12), then average the rest. The streak counts consecutive UTC days with at least one run, and stays alive until the end of the day after the last run.

### `GET /api/v1/leaderboard?limit=20`

Public. The best completed timer single per user, cached for 30 s. Times are self-reported; only runs with a scramble and at least 3 s count.

```json
{ "entries": [{ "rank": 1, "displayName": "Sourav", "bestMs": 18000, "solves": 5 }], "generatedAt": "...", "selfReported": true }
```

## Rate limits

These are per client IP and configurable through environment variables. Over the limit you get 429 with `Retry-After`.

| Scope | Default |
| --- | --- |
| All routes except health/metrics | 10 req/s, burst 40 |
| `/solve`, `/scramble` | 2 req/s, burst 10 |
| `/auth/*` | 10 per minute |
| `POST /runs`, `/runs/batch` | 30 per minute per account; at most 5,000 saved runs per account |
