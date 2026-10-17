---
name: thought-logger
description: >-
  Extracts and logs the AI agent's internal reasoning ("thought for {n} seconds")
  and timestamps to doc/ThoughtLog.md from conversation transcripts. Use when
  the user asks to record thoughts, track analytical decisions, document the
  reasoning process, or audit agent actions with timestamps.
---

# Thought Logger Skill

This skill extracts the agent's internal chain-of-thought ("thought for {n} seconds") notes, timestamps, and tool actions from Antigravity's session transcripts and formats them into a clean, chronological Markdown log (`doc/ThoughtLog.md`).

## Why This Exists

During complex architecture, coding, or debugging workflows, the internal reasoning notes ("thought for {n} seconds") capture critical technical decisions, trade-off evaluations, and problem-solving steps. By default, these notes are rendered in collapsible UI blocks and stored in the background transcript. This skill extracts and organizes them into a persistent project artifact.

---

## Directory Structure

```text
.agents/skills/thought-logger/
├── SKILL.md                          # This instruction guide
├── scripts/
│   └── log_thoughts.py               # Extraction & formatting utility
└── resources/
    └── hooks.example.json            # Template for automatic hook execution
```

---

## Usage

### 1. Manual / On-Demand Sync

Run the bundled extraction script from the repository root:

```bash
python3 .agents/skills/thought-logger/scripts/log_thoughts.py
```

Options:
- `--output <path>`: Destination Markdown file (defaults to `doc/ThoughtLog.md`).
- `--conversation-id <id>`: Target a specific session ID (defaults to the active/most recent session).
- `--transcript <path>`: Direct path to `transcript_full.jsonl` or `transcript.jsonl`.
- `--workspace <path>`: Target workspace directory root (defaults to CWD).

### 2. Full Automation via Lifecycle Hooks

To have `doc/ThoughtLog.md` automatically update in the background after every agent response, add a hook to `.agents/hooks.json`:

```json
{
  "thought-auto-logger": {
    "PostInvocation": [
      {
        "type": "command",
        "command": "python3 .agents/skills/thought-logger/scripts/log_thoughts.py"
      }
    ]
  }
}
```

The script automatically detects hook payload input on `stdin`, reads `transcriptPath` and `workspacePaths`, updates `doc/ThoughtLog.md`, and outputs `{}` for seamless execution.

### 3. Sharing Across Similar Projects

To use this skill in other projects:

1. **Local project install**: Copy the `.agents/skills/thought-logger/` directory into any new repository's root.
2. **Global install**: Copy the `thought-logger/` folder into your global config directory:
   `~/.gemini/config/skills/thought-logger/`
   This makes the skill accessible across all workspaces on your machine.
3. **Optional Project Rule (`AGENTS.md`)**: Add a rule in the root `AGENTS.md`:
   ```markdown
   # Thought Logging
   - After completing major task milestones or architectural analysis, run `python3 .agents/skills/thought-logger/scripts/log_thoughts.py` to keep `doc/ThoughtLog.md` up to date.
   ```

---

## Log Output Format

The output file `doc/ThoughtLog.md` is structured chronologically by interaction turn:

- **Turn Header**: User prompt, request context, and timestamp in local time.
- **Step Header**: Step index and timestamp.
- **Actions Taken**: Tools invoked during that step (e.g. `view_file`, `run_command`).
- **Internal Chain-of-Thought**: The unedited reasoning process and technical evaluations.
