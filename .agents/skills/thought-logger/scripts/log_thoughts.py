#!/usr/bin/env python3
"""
Thought Logger - Extracts model internal chain-of-thought ("thought for {n} seconds")
notes and timestamps from Antigravity transcripts and writes a structured log to doc/ThoughtLog.md.

Compatible with:
1. Direct CLI execution: `python3 log_thoughts.py [--output doc/ThoughtLog.md]`
2. Antigravity lifecycle hooks: Invoked via hooks.json (PostInvocation or Stop).
"""

import argparse
from datetime import datetime, timezone
import glob
import json
import os
import re
import sys


def find_latest_transcript(brain_dir=None):
    """Finds the most recently modified transcript_full.jsonl in the brain directory."""
    if not brain_dir:
        brain_dir = os.path.expanduser("~/.gemini/antigravity/brain")
    if not os.path.isdir(brain_dir):
        return None

    # Search for all transcript files
    pattern = os.path.join(brain_dir, "*", ".system_generated", "logs", "transcript_full.jsonl")
    files = glob.glob(pattern)
    if not files:
        # Fallback to transcript.jsonl
        pattern = os.path.join(brain_dir, "*", ".system_generated", "logs", "transcript.jsonl")
        files = glob.glob(pattern)

    if not files:
        return None

    # Sort by modification time, newest first
    files.sort(key=lambda p: os.path.getmtime(p), reverse=True)
    return files[0]


def parse_timestamp(iso_str):
    """Parses an ISO 8601 timestamp string and returns formatted local time string."""
    if not iso_str:
        return "Unknown Time"
    try:
        clean_iso = iso_str.replace("Z", "+00:00")
        dt_utc = datetime.fromisoformat(clean_iso)
        dt_local = dt_utc.astimezone()
        return dt_local.strftime("%Y-%m-%d %H:%M:%S %Z")
    except Exception:
        return iso_str


def clean_user_prompt(text):
    """Strips internal system/wrapper XML tags from user input for clean display."""
    if not text:
        return ""
    if "<USER_REQUEST>" in text and "</USER_REQUEST>" in text:
        match = re.search(r"<USER_REQUEST>(.*?)</USER_REQUEST>", text, re.DOTALL)
        if match:
            return match.group(1).strip()
    return text.strip()


def extract_thoughts_from_transcript(transcript_path):
    """Extracts user turns, thought blocks, timestamps, and tool actions from the transcript."""
    if not os.path.isfile(transcript_path):
        raise FileNotFoundError(f"Transcript file not found: {transcript_path}")

    # Derive conversation ID from path if possible
    conv_id = "unknown"
    parts = transcript_path.split(os.sep)
    if ".system_generated" in parts:
        idx = parts.index(".system_generated")
        if idx > 0:
            conv_id = parts[idx - 1]

    turns = []
    current_turn = None

    with open(transcript_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                entry = json.loads(line)
            except json.JSONDecodeError:
                continue

            step_type = entry.get("type")
            step_index = entry.get("step_index", 0)
            created_at = entry.get("created_at")

            if step_type == "USER_INPUT":
                raw_content = entry.get("content", "")
                user_msg = clean_user_prompt(raw_content)
                current_turn = {
                    "turn_index": len(turns) + 1,
                    "user_prompt": user_msg,
                    "started_at": created_at,
                    "steps": [],
                }
                turns.append(current_turn)

            elif step_type == "PLANNER_RESPONSE":
                thinking = entry.get("thinking")
                tool_calls = entry.get("tool_calls", [])

                if thinking:
                    if current_turn is None:
                        current_turn = {
                            "turn_index": 1,
                            "user_prompt": "(Initial Session Start)",
                            "started_at": created_at,
                            "steps": [],
                        }
                        turns.append(current_turn)

                    # Summarize tool calls
                    tools_summary = []
                    for tc in tool_calls:
                        t_name = tc.get("name", "tool")
                        t_args = tc.get("args", {})
                        if isinstance(t_args, str):
                            try:
                                t_args = json.loads(t_args)
                            except Exception:
                                t_args = {}

                        action = t_args.get("toolAction") or t_args.get("toolSummary")
                        if action:
                            tools_summary.append(f"`{t_name}` ({action.strip()})")
                        else:
                            # Highlight useful args like command or filename
                            cmd = t_args.get("CommandLine")
                            file_path = t_args.get("AbsolutePath") or t_args.get("TargetFile")
                            if cmd:
                                short_cmd = (cmd[:40] + "...") if len(cmd) > 40 else cmd
                                tools_summary.append(f"`{t_name}`: `{short_cmd}`")
                            elif file_path:
                                short_file = os.path.basename(file_path)
                                tools_summary.append(f"`{t_name}`: `{short_file}`")
                            else:
                                tools_summary.append(f"`{t_name}`")

                    current_turn["steps"].append({
                        "step_index": step_index,
                        "created_at": created_at,
                        "thinking": thinking.strip(),
                        "tools": tools_summary,
                    })

    return conv_id, turns


def generate_markdown(conv_id, turns):
    """Generates a formatted markdown string representing the thought log."""
    now_local = datetime.now().astimezone().strftime("%Y-%m-%d %H:%M:%S %Z")

    md = [
        "# Agent Thought & Analysis Log",
        "",
        f"> **Session ID:** `{conv_id}`  ",
        f"> **Last Synced:** {now_local}  ",
        "> **Description:** Chronological log of the AI agent's internal reasoning (\"thought for {n} seconds\"), "
        "timestamps, and operational actions captured from conversation transcripts.",
        "",
        "---",
        "",
    ]

    total_thought_steps = sum(len(t["steps"]) for t in turns)
    if total_thought_steps == 0:
        md.append("_No internal thought steps recorded yet._\n")
        return "\n".join(md)

    for turn in turns:
        if not turn["steps"]:
            continue

        turn_time = parse_timestamp(turn["started_at"])
        md.append(f"## Turn {turn['turn_index']}: User Request")
        md.append(f"**Timestamp:** `{turn_time}`\n")
        
        # Quote prompt cleanly
        prompt_lines = turn["user_prompt"].split("\n")
        quoted_prompt = "\n".join(f"> {line}" for line in prompt_lines if line.strip())
        if quoted_prompt:
            md.append("**Prompt:**")
            md.append(quoted_prompt)
            md.append("")

        for step in turn["steps"]:
            step_time = parse_timestamp(step["created_at"])
            md.append(f"### Step {step['step_index']} — Analysis ({step_time})")

            if step["tools"]:
                tools_str = ", ".join(step["tools"])
                md.append(f"**Action Taken:** {tools_str}\n")

            md.append("**Internal Chain-of-Thought:**")
            thought_text = step["thinking"]
            formatted_thought = "\n".join(f"> {line}" if line else ">" for line in thought_text.split("\n"))
            md.append(formatted_thought)
            md.append("")

        md.append("---")
        md.append("")

    return "\n".join(md)


def main():
    parser = argparse.ArgumentParser(description="Log agent thought processes to markdown.")
    parser.add_argument("--transcript", help="Path to transcript.jsonl or transcript_full.jsonl")
    parser.add_argument("--conversation-id", help="Conversation ID to locate transcript for")
    parser.add_argument("--output", help="Target markdown file (default: doc/ThoughtLog.md)")
    parser.add_argument("--workspace", help="Workspace root directory")
    args = parser.parse_args()

    # Check if invoked via hook with payload on stdin
    hook_data = {}
    if not sys.stdin.isatty():
        try:
            stdin_content = sys.stdin.read()
            if stdin_content.strip():
                hook_data = json.loads(stdin_content)
        except Exception:
            hook_data = {}

    # Determine transcript path
    transcript_path = args.transcript or hook_data.get("transcriptPath")
    if not transcript_path and args.conversation_id:
        transcript_path = os.path.expanduser(
            f"~/.gemini/antigravity/brain/{args.conversation_id}/.system_generated/logs/transcript_full.jsonl"
        )
    if not transcript_path:
        transcript_path = find_latest_transcript()

    if not transcript_path or not os.path.isfile(transcript_path):
        # Fall back to checking transcript_full.jsonl if transcript.jsonl was given
        if transcript_path and transcript_path.endswith("transcript.jsonl"):
            full_path = transcript_path.replace("transcript.jsonl", "transcript_full.jsonl")
            if os.path.isfile(full_path):
                transcript_path = full_path

    if not transcript_path or not os.path.isfile(transcript_path):
        sys.stderr.write("Error: Could not locate a valid transcript file.\n")
        if hook_data:
            print(json.dumps({}))
        sys.exit(1)

    # If transcript.jsonl was provided, prefer transcript_full.jsonl to avoid truncation
    if transcript_path.endswith("transcript.jsonl"):
        alt_full = transcript_path.replace("transcript.jsonl", "transcript_full.jsonl")
        if os.path.isfile(alt_full):
            transcript_path = alt_full

    # Determine workspace and output path
    workspace = args.workspace
    if not workspace and hook_data.get("workspacePaths"):
        workspace = hook_data["workspacePaths"][0]
    if not workspace:
        workspace = os.getcwd()

    output_path = args.output
    if not output_path:
        output_path = os.path.join(workspace, "doc", "ThoughtLog.md")

    # Ensure output directory exists
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)

    # Process and write
    try:
        conv_id, turns = extract_thoughts_from_transcript(transcript_path)
        markdown_content = generate_markdown(conv_id, turns)

        with open(output_path, "w", encoding="utf-8") as f:
            f.write(markdown_content)

        sys.stderr.write(f"Successfully synced {sum(len(t['steps']) for t in turns)} thought steps to {output_path}\n")

        # Submodule Context: Mirror relevant turns to lib/scpi/doc/ThoughtLog.md if present
        scpi_doc_dir = os.path.join(workspace, "lib", "scpi", "doc")
        if os.path.isdir(scpi_doc_dir):
            scpi_turns = []
            for t in turns:
                t_matches = False
                for s in t.get("steps", []):
                    if any("lib/scpi" in tool for tool in s.get("tools", [])) or "lib/scpi" in s.get("thinking", "") or "scpi" in t.get("user_prompt", "").lower():
                        t_matches = True
                        break
                if t_matches:
                    scpi_turns.append(t)
            if scpi_turns:
                scpi_output = os.path.join(scpi_doc_dir, "ThoughtLog.md")
                scpi_md = generate_markdown(conv_id, scpi_turns)
                with open(scpi_output, "w", encoding="utf-8") as f:
                    f.write(scpi_md)
                sys.stderr.write(f"Synced {sum(len(t['steps']) for t in scpi_turns)} thought steps to {scpi_output}\n")
    except Exception as e:
        sys.stderr.write(f"Error logging thoughts: {e}\n")
        if hook_data:
            print(json.dumps({}))
        sys.exit(1)

    # Return empty JSON object for hook compatibility
    if hook_data:
        print(json.dumps({}))


if __name__ == "__main__":
    main()
