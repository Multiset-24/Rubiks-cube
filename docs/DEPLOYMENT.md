# Deployment

Everything runs on free tiers:

| Part | Host | Config |
| --- | --- | --- |
| Frontend | Netlify | `netlify.toml` |
| API | Render (Docker web service) | `render.yaml`, `backend/Dockerfile` |
| Database | Neon (Postgres) | `DATABASE_URL` |

The database isn't on Render because Render's free Postgres is deleted after 30 days; Neon's free tier doesn't expire.

## 1. Database (Neon), about 2 minutes

1. Sign in at https://neon.tech (GitHub login works) and create a project. Pick the region closest to your Render region; `render.yaml` uses Singapore.
2. Copy the connection string from the dashboard. It looks like `postgresql://user:password@ep-xxx.ap-southeast-1.aws.neon.tech/neondb?sslmode=require`.

You don't need to create tables. The API runs its migrations on startup.

## 2. API (Render), about 5 minutes for the first build

1. Sign in at https://render.com with GitHub and allow access to this repository.
2. Click **New → Blueprint**, pick this repository, and the branch you want to deploy (`main` once merged).
3. Render reads `render.yaml` and asks for `DATABASE_URL`. Paste the Neon connection string. `JWT_SECRET` is generated for you.
4. Click **Apply**.

The Docker build compiles the server, runs the test suite and precomputes the corner pattern database, so expect several minutes the first time. Render switches traffic to a new deploy only after `/readyz` returns 200, so a deploy that can't reach the database never goes live.

The service URL will be `https://zen-cube-solver-api.onrender.com`, unless that name is taken; then Render adds a suffix. Check it:

```bash
curl https://zen-cube-solver-api.onrender.com/readyz
# {"status":"ready","storage":"postgres"}
```

The free instance sleeps after 15 minutes without traffic and takes about a minute to wake. The frontend pings the API on page load and shows "Server starting" while it wakes.

## 3. Frontend (Netlify)

The site is `https://zen-cube-solver.netlify.app`. To deploy from scratch: **Add new site → Import an existing project**, pick the repo, and Netlify reads `netlify.toml`. It builds from `frontend/`, runs the unit tests and publishes `frontend/dist`.

If your Render URL differs from the one above, change `VITE_API_URL` in `netlify.toml` (or set it in Netlify's environment variables) and redeploy. Also add your Netlify URL to `CORS_ORIGINS` in Render if you renamed the site.

You can point a deployed frontend at another API without rebuilding by adding `?api=https://your-api.example.com` to the URL. It is remembered in that browser.

Netlify injects its own toolbar script into production pages. The site's CSP blocks the inline code it tries to run, which shows up as a console message but doesn't affect the app. You can turn the toolbar off in the site's settings on Netlify (the Netlify Drawer / toolbar option).

## Environment variables (API)

| Variable | Default | Notes |
| --- | --- | --- |
| `PORT` | 8080 | Render sets this |
| `DATABASE_URL` | (empty) | Postgres URL. Empty means an in-memory store (data is lost on restart) |
| `JWT_SECRET` | random per start | Required when `DATABASE_URL` is set |
| `JWT_TTL_HOURS` | 168 | Session length |
| `PASSWORD_HASH_ITERATIONS` | 150000 | PBKDF2 iterations; old hashes are upgraded on login |
| `CORS_ORIGINS` | `*` | Comma-separated; supports `https://*--site.netlify.app` patterns |
| `TRUST_PROXY` | true | Read the client IP from `X-Forwarded-For` (true behind Render) |
| `WORKER_THREADS` | 8 | HTTP worker threads |
| `MAX_CONCURRENT_SOLVES` | 4 (2 on Render) | Solve bulkhead size |
| `MAX_CONCURRENT_OPTIMAL` | 1 | Optimal-search bulkhead size |
| `MAX_CONCURRENT_AUTH` | 2 | Concurrent password hashes |
| `SOLVE_TIMEOUT_MS` | 4000 | Deadline for beginner and Kociemba |
| `OPTIMAL_TIMEOUT_MS` | 3000 | Deadline before Optimal falls back to Kociemba |
| `QUEUE_TIMEOUT_MS` | 2000 | How long a request waits for a bulkhead slot before 503 |
| `SOLVE_CACHE_SIZE` | 2000 | LRU entries |
| `RATE_LIMIT_RPS` / `RATE_LIMIT_BURST` | 10 / 40 | Per IP, all routes |
| `SOLVE_RATE_RPS` / `SOLVE_RATE_BURST` | 2 / 10 | Per IP, solve and scramble |
| `AUTH_RATE_PER_MINUTE` | 10 | Per IP, login and register |
| `DB_POOL_SIZE` | 4 (3 on Render) | Postgres connections |
| `DATA_DIR` | `./data` (`/app/data` in Docker) | Where `corner.db` lives |
| `LOG_LEVEL` | info | debug, info, warn, error |

## Running the production image locally

```bash
docker build -f backend/Dockerfile -t zen-cube-api .
docker run --rm -p 8080:8080 \
  -e DATABASE_URL="postgresql://..." -e JWT_SECRET="$(openssl rand -hex 32)" \
  --cpus=0.1 --memory=512m zen-cube-api      # same limits as Render's free tier
```

With those limits the container is ready in about 5 seconds and uses about 52 MB of memory.

## CI

`.github/workflows/ci.yml` runs on every push and pull request:

- **backend**: builds with CMake and runs all C++ tests, including the Postgres contract tests against a `postgres:16` service container and the full 88M-state database check;
- **frontend**: runs `npm ci`, the type check, Vitest and a production build;
- **docker**: builds the image (which runs the tests again), starts it and makes a real solve request.
