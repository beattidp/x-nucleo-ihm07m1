# Project Guidelines & Rules

## Git Version Control & Commit Cadence
- **Commit after each iteration**: Whenever changes are made to code, features, or fixes, create a Git commit after each prompt and response.
- **Tag phase milestones**: When completing major functional milestones, create a git tag.
- **Review and approve**: Request a review of the commit log and obtain approval before pushing upstream.
- **Squash as necessary**: Maintain a clean, logical commit history.

## Documentation & Worklogs
- **Update documentation**: After each prompt and response, update relevant files in the `doc/` directory.
- **Worklogs (`doc/worklogs/`)**: Maintain detailed activity logs in `doc/worklogs/`. Each entry must include the date, time, agent, and user, summarizing all actions and changes.
- **Feature & Fix Documentation (`doc/features/`)**: Document all implemented, refined, or fixed features in `doc/features/` with comprehensive details on architecture, API usage, and verification.

## Thought & Analysis Logging
- This repository tracks internal chain-of-thought reasoning ("thought for {n} seconds") and operational decisions in [`doc/ThoughtLog.md`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/doc/ThoughtLog.md).
- Automatic logging is configured via `.agents/hooks.json` on `PostInvocation`.
- To manually refresh or audit the thought log at any time, run:
  ```bash
  python3 .agents/skills/thought-logger/scripts/log_thoughts.py
  ```
