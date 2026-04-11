# vMaNGOS + Playerbots Branch Workflow

This repo tracks `vMaNGOS + Playerbots` with AH work layered on top.

## Branch Hierarchy

1. `upstream/vmangos-ike3-playerbots`
   - Upstream source of truth for playerbots behavior and feature direction.
2. `codex/vmangos-playerbots-catchup`
   - Downstream integration branch for absorbing upstream playerbots commits.
   - This is where Debian 13, WSL, and Linux build compatibility work belongs.
3. `ahbot-real-buyer-seller-phase1`
   - First AH checkpoint built on the verified catch-up branch.
4. `ahbot-real-buyer-seller-phase2`
   - Next AH pass built on the verified `phase1` tip.

## Rules

- Do not start AH work directly from `development`.
- Do not start AH work directly from `upstream/vmangos-ike3-playerbots`.
- Upstream playerbots changes land in `codex/vmangos-playerbots-catchup` first.
- Linux and Debian compatibility fixes also land in `codex/vmangos-playerbots-catchup`.
- AH feature branches begin only after the catch-up branch builds cleanly.
- Build in WSL before committing or pushing.
- Treat the git-backed checkout as the source of truth.

## Upstream Catch-up Flow

```bash
git checkout codex/vmangos-bot-loot-roll-port
git checkout -B codex/vmangos-playerbots-catchup
```

Then absorb upstream playerbots work from `upstream/vmangos-ike3-playerbots`.

Preferred approach:

```bash
git cherry-pick <upstream-playerbots-commit> ...
```

Use this branch to:

- bring in upstream playerbots commits from `ileboii/core`
- fix Debian 13 and Linux portability issues
- verify WSL and prox-compatible builds

## AH Feature Flow

After `codex/vmangos-playerbots-catchup` is clean and builds:

```bash
git checkout codex/vmangos-playerbots-catchup
git pull --ff-only
git checkout -b ahbot-real-buyer-seller-phase1
```

Do the `phase1` AH work, then build and push it.

After `phase1` is verified:

```bash
git checkout ahbot-real-buyer-seller-phase1
git pull --ff-only
git checkout -b ahbot-real-buyer-seller-phase2
```

Do the `phase2` AH work there.

## Build Flow

Build in WSL from the active repo checkout:

```bash
cd /mnt/c/Git/SPP/vmangos/ahbot-real-buyer-seller-phase1_fresh
rm -rf build-wsl
mkdir build-wsl
cd build-wsl
cmake .. \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_PLAYERBOTS=ON \
  -DSUPPORTED_CLIENT_BUILD=5875 \
  -DMYSQL_INCLUDE_DIR=/usr/include/mariadb \
  -DMYSQL_LIBRARY=/usr/lib/x86_64-linux-gnu/libmariadb.so
make -j"$(nproc)"
```

## Current Local Path

Until the broken recovery folder is retired, use this as the healthy git-backed checkout:

`C:\Git\SPP\vmangos\ahbot-real-buyer-seller-phase1_fresh`

## GitHub Setting

The canonical repo is:

`https://github.com/japtenks/SPP-Vmangos-nix`

The GitHub default branch should point at the current playerbots downstream base, not `development`.
