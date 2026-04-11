# Agent Notes

## Repo Role

This is a downstream `vMaNGOS + Playerbots` integration repo.

The upstream gameplay and playerbots source of truth is:

- `ileboii/core`
- branch: `vmangos-ike3-playerbots`

## Branch Policy

- `upstream/vmangos-ike3-playerbots`
  - authoritative upstream for playerbots behavior
- `codex/vmangos-playerbots-catchup`
  - absorb upstream playerbots commits here first
  - apply Debian 13, WSL, and Linux compatibility fixes here
- `ahbot-real-buyer-seller-phase1`
  - AH work built on the verified catch-up branch
- `ahbot-real-buyer-seller-phase2`
  - follow-up AH work built on verified `phase1`

## Implementation Guidance

- Do not implement AH features directly on top of `development`.
- Do not merge upstream playerbots changes directly into AH feature branches.
- Update the playerbots compatibility branch first, build it, then replay AH branches on top.
- Prefer small, reviewable upstream catch-up steps, usually cherry-picks, when upstream and downstream have diverged.
- Keep generated build output out of commits.
- Build and verify in WSL.

## Local Working Repo

Use this checkout as the source of truth:

- `C:\Git\SPP\vmangos\ahbot-real-buyer-seller-phase1_fresh`

Do not use the old broken recovery folder for active work.
