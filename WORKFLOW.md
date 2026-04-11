# AH Bot Branch Workflow

This repo uses a simple branch ladder so each AH bot phase has a stable base.

## Branch Roles

- `codex/vmangos-bot-loot-roll-port`
  - Baseline branch for AH bot work.
- `ahbot-real-buyer-seller-phase1`
  - First deployable AH bot checkpoint.
- `ahbot-real-buyer-seller-phase2`
  - Next pass built on the verified `phase1` branch.

## Rules

- Do not start AH work from `development`.
- Always branch from `codex/vmangos-bot-loot-roll-port` for a new phase 1 line.
- Always branch `phase2` from the verified `phase1` tip.
- Build in WSL before committing or pushing.
- Treat the git-backed checkout as the source of truth.

## Phase 1 Flow

```bash
git checkout codex/vmangos-bot-loot-roll-port
git pull --ff-only
git checkout -b ahbot-real-buyer-seller-phase1
```

Do the phase 1 work.

Build in WSL from the repo checkout:

```bash
cd /mnt/c/Git/SPP/vmangos/ahbot-real-buyer-seller-phase1_fresh
mkdir -p build-wsl
cd build-wsl
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build . -j"$(nproc)"
```

Then commit and push:

```bash
git add .
git commit -m "AHBot: phase 1"
git push -u origin ahbot-real-buyer-seller-phase1
```

## Phase 2 Flow

After phase 1 is built, verified, and pushed:

```bash
git checkout ahbot-real-buyer-seller-phase1
git pull --ff-only
git checkout -b ahbot-real-buyer-seller-phase2
```

Do the phase 2 work.

Build in WSL again, then commit and push:

```bash
git add .
git commit -m "AHBot: phase 2"
git push -u origin ahbot-real-buyer-seller-phase2
```

## Current Local Path

Until the locked broken folder is retired, use this as the healthy git-backed checkout:

`C:\Git\SPP\vmangos\ahbot-real-buyer-seller-phase1_fresh`

## GitHub Setting

Set the GitHub default branch to:

`codex/vmangos-bot-loot-roll-port`
