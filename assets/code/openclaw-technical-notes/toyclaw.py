#!/usr/bin/env python3
"""ToyClaw: a tiny OpenClaw-like CLI assistant.

Everything lives in this single file on purpose:
- interactive TUI-ish REPL
- workspace rooted at /tmp/toyclaw
- context files: USER.md, SOUL.md, IDENTITY.md, AGENT.md
- skills installed as plain markdown files
- minimal shell tool support driven by an OpenAI-compatible chat API
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import textwrap
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable
from urllib.error import HTTPError, URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen


WORKSPACE = Path("/tmp/toyclaw")
SKILLS_DIR = WORKSPACE / "skills"
SESSION_LOG = WORKSPACE / "session.jsonl"
DEFAULT_CONTEXT_FILES = ("USER.md", "SOUL.md", "IDENTITY.md", "AGENT.md")
CONTEXT_FILE_ALIASES = {
    "USER.md": ("USER.md",),
    "SOUL.md": ("SOUL.md",),
    "IDENTITY.md": ("IDENTITY.md",),
    "AGENT.md": ("AGENT.md", "AGENTS.md"),
    "AGENTS.md": ("AGENT.md", "AGENTS.md"),
}
DEFAULT_MODEL = "gpt-4.1-mini"
MAX_TOOL_STEPS = 4
MAX_OUTPUT_CHARS = 12000
SHELL_TIMEOUT_SECONDS = 15

DEFAULT_FILE_CONTENTS = {
    "USER.md": textwrap.dedent(
        """\
        # USER.md

        Describe the human you are helping here.
        Examples:
        - name / nickname
        - language preference
        - working style
        - constraints to remember
        """
    ),
    "SOUL.md": textwrap.dedent(
        """\
        # SOUL.md

        Define the assistant's values, personality, and tone here.
        """
    ),
    "IDENTITY.md": textwrap.dedent(
        """\
        # IDENTITY.md

        Define the assistant's public identity here.
        Example:
        - name
        - vibe
        - style
        """
    ),
    "AGENT.md": textwrap.dedent(
        """\
        # AGENT.md

        Operating notes:
        - help the user directly
        - keep answers concise
        - use shell only when it materially helps
        - avoid destructive commands
        """
    ),
}


def read_env(name: str, fallback: str | None = None) -> str | None:
    value = os.environ.get(name)
    if value:
        return value
    return fallback


def normalize_api_url(raw: str | None) -> str:
    base = (raw or read_env("OPENAI_BASE_URL") or "https://api.openai.com/v1").rstrip("/")
    if base.endswith("/chat/completions"):
        return base
    if base.endswith("/v1"):
        return f"{base}/chat/completions"
    return f"{base}/v1/chat/completions"


def ensure_workspace() -> None:
    WORKSPACE.mkdir(parents=True, exist_ok=True)
    SKILLS_DIR.mkdir(parents=True, exist_ok=True)
    for name, content in DEFAULT_FILE_CONTENTS.items():
        path = WORKSPACE / name
        if not path.exists():
            path.write_text(content, encoding="utf-8")


def load_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8").strip()
    except FileNotFoundError:
        return ""


def save_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content.rstrip() + "\n", encoding="utf-8")


def resolve_context_path(name: str) -> Path:
    aliases = CONTEXT_FILE_ALIASES.get(name, (name,))
    for alias in aliases:
        candidate = WORKSPACE / alias
        if candidate.exists():
            return candidate
    return WORKSPACE / aliases[0]


def sanitize_filename(name: str) -> str:
    safe = re.sub(r"[^A-Za-z0-9._-]+", "-", name).strip("-._")
    return safe or "skill"


def truncate(text: str, limit: int = MAX_OUTPUT_CHARS) -> str:
    if len(text) <= limit:
        return text
    omitted = len(text) - limit
    return f"{text[:limit]}\n\n[truncated {omitted} characters]"


def print_block(title: str, content: str) -> None:
    print(f"\n== {title} ==")
    print(content.rstrip() if content.strip() else "(empty)")


def append_session_log(role: str, content: str) -> None:
    record = {"role": role, "content": content}
    with SESSION_LOG.open("a", encoding="utf-8") as fh:
        fh.write(json.dumps(record, ensure_ascii=False) + "\n")


def extract_first_json_object(text: str) -> dict | None:
    decoder = json.JSONDecoder()
    for index, char in enumerate(text):
        if char != "{":
            continue
        try:
            obj, _ = decoder.raw_decode(text[index:])
        except json.JSONDecodeError:
            continue
        if isinstance(obj, dict):
            return obj
    return None


def install_skill(source: str) -> Path:
    parsed = urlparse(source)
    if parsed.scheme in {"http", "https"}:
        request = Request(source, headers={"User-Agent": "ToyClaw/0.1"})
        with urlopen(request, timeout=20) as response:
            content = response.read().decode("utf-8")
        stem = Path(parsed.path or "skill.md").name
    else:
        local_path = Path(source).expanduser()
        content = local_path.read_text(encoding="utf-8")
        stem = local_path.name

    if not stem.endswith(".md"):
        stem += ".md"

    target = SKILLS_DIR / sanitize_filename(stem)
    save_text(target, content)
    return target


def list_skills() -> list[Path]:
    return sorted(SKILLS_DIR.glob("*.md"))


def format_installed_skill_names() -> str:
    skills = list_skills()
    if not skills:
        return "(no installed skills)"
    return "\n".join(f"- {skill.name}" for skill in skills)


def maybe_handle_local_query(user_input: str) -> str | None:
    normalized = re.sub(r"\s+", " ", user_input.strip().lower())
    skill_markers = ("skill", "skills", "技能")
    list_markers = (
        "list my",
        "what",
        "show my",
        "installed",
        "有哪些",
        "列出",
        "看看",
        "显示",
        "已安装",
    )
    if any(marker in normalized for marker in skill_markers) and any(marker in normalized for marker in list_markers):
        return "Installed skills:\n" + format_installed_skill_names()
    return None


def format_context_block(path: Path) -> str:
    content = load_text(path)
    if not content:
        return ""
    return f"\n## {path.name}\n{content}\n"


def build_system_prompt() -> str:
    sections = []
    for name in DEFAULT_CONTEXT_FILES:
        block = format_context_block(resolve_context_path(name))
        if block:
            sections.append(block)

    skills = []
    for skill in list_skills():
        content = load_text(skill)
        if content:
            skills.append(f"\n## Skill: {skill.name}\n{content}\n")

    skills_text = "".join(skills) if skills else "\n(no installed skills)\n"
    skill_name_list = format_installed_skill_names()
    context_text = "".join(sections) if sections else "\n(no workspace context files)\n"

    return textwrap.dedent(
        f"""\
        You are ToyClaw, a tiny OpenClaw-like assistant running in a CLI.
        Workspace root: {WORKSPACE}

        Respond with exactly one JSON object and no surrounding markdown.

        If you can answer directly:
        {{"type":"answer","content":"your reply"}}

        If you need a shell command:
        {{"type":"shell","command":"your command","reason":"brief reason"}}

        Rules:
        - Use shell only when it clearly helps answer the user.
        - Shell runs inside {WORKSPACE}.
        - Prefer short, non-destructive commands.
        - Never use destructive commands, privilege escalation, background jobs, or interactive programs.
        - After receiving shell output, continue and either ask for another command or provide the final answer.
        - Keep final answers concise and useful.

        Loaded workspace context:
        {context_text}

        Installed skill file names:
        {skill_name_list}

        Installed skills:
        {skills_text}
        """
    ).strip()


def is_dangerous_shell(command: str) -> str | None:
    stripped = command.strip()
    if not stripped:
        return "empty command"

    dangerous_patterns = [
        r"(^|[;&|])\s*rm\s+-rf\b",
        r"(^|[;&|])\s*sudo\b",
        r"(^|[;&|])\s*su\b",
        r"(^|[;&|])\s*shutdown\b",
        r"(^|[;&|])\s*reboot\b",
        r"(^|[;&|])\s*poweroff\b",
        r"(^|[;&|])\s*mkfs\b",
        r"(^|[;&|])\s*dd\b",
        r":\(\)\s*\{\s*:\|:\s*&\s*\};:",
    ]
    for pattern in dangerous_patterns:
        if re.search(pattern, stripped):
            return f"blocked by safety rule: {pattern}"

    try:
        head = shlex.split(stripped)[0]
    except ValueError:
        return "invalid shell syntax"

    blocked_binaries = {"vim", "vi", "nano", "less", "more", "top", "htop", "python", "python3"}
    if head in blocked_binaries:
        return f"blocked interactive or unrestricted binary: {head}"
    return None


def run_shell(command: str) -> str:
    blocked = is_dangerous_shell(command)
    if blocked:
        return f"COMMAND BLOCKED\nReason: {blocked}\nCommand: {command}"

    completed = subprocess.run(
        ["bash", "-lc", command],
        cwd=WORKSPACE,
        capture_output=True,
        text=True,
        timeout=SHELL_TIMEOUT_SECONDS,
    )
    stdout = truncate(completed.stdout or "")
    stderr = truncate(completed.stderr or "")
    return textwrap.dedent(
        f"""\
        Command: {command}
        Exit code: {completed.returncode}
        Stdout:
        {stdout if stdout else '(empty)'}

        Stderr:
        {stderr if stderr else '(empty)'}
        """
    ).strip()


@dataclass
class ChatConfig:
    model: str
    api_key: str
    api_url: str


class ChatClient:
    def __init__(self, config: ChatConfig) -> None:
        self.config = config

    def complete(self, messages: list[dict[str, str]]) -> str:
        payload = {
            "model": self.config.model,
            "messages": messages,
            "temperature": 0.2,
        }
        body = json.dumps(payload).encode("utf-8")
        headers = {
            "Content-Type": "application/json",
            "Authorization": f"Bearer {self.config.api_key}",
        }
        request = Request(self.config.api_url, data=body, headers=headers, method="POST")
        with urlopen(request, timeout=120) as response:
            raw = response.read().decode("utf-8")
        data = json.loads(raw)
        return data["choices"][0]["message"]["content"]


def run_agent_turn(client: ChatClient, history: list[dict[str, str]], user_input: str) -> str:
    system_prompt = build_system_prompt()
    working_messages = list(history)
    working_messages.append({"role": "user", "content": user_input})

    for _ in range(MAX_TOOL_STEPS):
        response_text = client.complete([{"role": "system", "content": system_prompt}] + working_messages)
        action = extract_first_json_object(response_text)
        if not action:
            history.extend(
                [
                    {"role": "user", "content": user_input},
                    {"role": "assistant", "content": response_text.strip()},
                ]
            )
            return response_text.strip()

        action_type = action.get("type")
        if action_type == "answer":
            content = str(action.get("content", "")).strip()
            history.extend(
                [
                    {"role": "user", "content": user_input},
                    {"role": "assistant", "content": content},
                ]
            )
            return content

        if action_type == "shell":
            command = str(action.get("command", "")).strip()
            shell_result = run_shell(command)
            print_block(f"shell: {command}", shell_result)
            working_messages.append({"role": "assistant", "content": json.dumps(action, ensure_ascii=False)})
            working_messages.append(
                {
                    "role": "user",
                    "content": "Shell result:\n" + shell_result,
                }
            )
            continue

        fallback = response_text.strip() or json.dumps(action, ensure_ascii=False)
        history.extend(
            [
                {"role": "user", "content": user_input},
                {"role": "assistant", "content": fallback},
            ]
        )
        return fallback

    timeout_message = "I reached the maximum number of shell steps for one turn. Please narrow the request."
    history.extend(
        [
            {"role": "user", "content": user_input},
            {"role": "assistant", "content": timeout_message},
        ]
    )
    return timeout_message


def prompt_multiline() -> str:
    print("Enter content. Finish with a single '.' on its own line.")
    lines = []
    while True:
        try:
            line = input()
        except EOFError:
            break
        if line == ".":
            break
        lines.append(line)
    return "\n".join(lines).rstrip() + "\n"


def handle_command(raw: str) -> bool:
    parts = raw.strip().split(maxsplit=2)
    if not parts:
        return True

    command = parts[0]
    if command in {"/quit", "/exit"}:
        return False

    if command == "/help":
        print(
            textwrap.dedent(
                """\
                Commands:
                  /help                    Show this help
                  /files                   List workspace files and installed skills
                  /show FILE               Show a workspace file
                  /edit FILE               Edit a workspace file (multiline, end with .)
                  /skill install SRC       Install a markdown skill from URL or local path
                  /skill list              List installed skills
                  /quit                    Exit
                """
            ).rstrip()
        )
        return True

    if command == "/files":
        print("\nWorkspace files:")
        for name in DEFAULT_CONTEXT_FILES:
            path = resolve_context_path(name)
            print(f"  - {path}")
        skills = list_skills()
        print("\nInstalled skills:")
        if skills:
            for path in skills:
                print(f"  - {path}")
        else:
            print("  (none)")
        return True

    if command == "/show" and len(parts) >= 2:
        target = resolve_context_path(parts[1])
        if not target.exists():
            print(f"File not found: {target}")
            return True
        print_block(str(target), load_text(target))
        return True

    if command == "/edit" and len(parts) >= 2:
        target_name = parts[1]
        if target_name not in CONTEXT_FILE_ALIASES:
            print("Only workspace context files are editable from /edit.")
            return True
        target = resolve_context_path(target_name)
        existing = load_text(target)
        if existing:
            print_block(f"current {target.name}", existing)
        new_content = prompt_multiline()
        save_text(target, new_content)
        print(f"Saved {target}")
        return True

    if command == "/skill":
        if len(parts) < 2:
            print("Usage: /skill install URL_OR_PATH | /skill list")
            return True
        subcommand = parts[1]
        if subcommand == "list":
            skills = list_skills()
            if not skills:
                print("No skills installed.")
            else:
                for path in skills:
                    print(path)
            return True
        if subcommand == "install" and len(parts) >= 3:
            try:
                installed = install_skill(parts[2])
            except (FileNotFoundError, HTTPError, URLError, OSError, UnicodeDecodeError) as exc:
                print(f"Skill install failed: {exc}")
                return True
            print(f"Installed skill: {installed}")
            return True
        print("Usage: /skill install URL_OR_PATH | /skill list")
        return True

    print(f"Unknown command: {raw}")
    return True


def print_banner(config: ChatConfig | None) -> None:
    print("ToyClaw")
    print(f"workspace: {WORKSPACE}")
    if config:
        print(f"model: {config.model}")
        print(f"api: {config.api_url}")
    else:
        print("model: not configured")
    print("Type /help for commands, /quit to exit.")


def build_config(args: argparse.Namespace) -> ChatConfig | None:
    api_key = args.api_key or read_env("TOYCLAW_API_KEY") or read_env("OPENAI_API_KEY")
    if not api_key:
        return None
    model = args.model or read_env("TOYCLAW_MODEL") or read_env("OPENAI_MODEL") or DEFAULT_MODEL
    api_url = normalize_api_url(args.api_url or read_env("TOYCLAW_API_URL"))
    return ChatConfig(model=model, api_key=api_key, api_url=api_url)


def print_context_preview() -> None:
    print(build_system_prompt())


def repl(client: ChatClient | None) -> int:
    history: list[dict[str, str]] = []
    while True:
        try:
            raw = input("\nyou> ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\nbye")
            return 0

        if not raw:
            continue

        if raw.startswith("/"):
            if not handle_command(raw):
                print("bye")
                return 0
            continue

        local_reply = maybe_handle_local_query(raw)
        if local_reply is not None:
            append_session_log("user", raw)
            append_session_log("assistant", local_reply)
            print(f"\nclaw> {local_reply}")
            continue

        if client is None:
            print("API is not configured. Set TOYCLAW_API_KEY or OPENAI_API_KEY first.")
            continue

        append_session_log("user", raw)
        try:
            reply = run_agent_turn(client, history, raw)
        except HTTPError as exc:
            print(f"HTTP error from model API: {exc.code} {exc.reason}")
            continue
        except URLError as exc:
            print(f"Network error from model API: {exc.reason}")
            continue
        except subprocess.TimeoutExpired:
            reply = "Shell command timed out."
        except Exception as exc:  # pragma: no cover - last-resort UX guard
            reply = f"ToyClaw error: {exc}"

        append_session_log("assistant", reply)
        print(f"\nclaw> {reply}")


def parse_args(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Tiny OpenClaw-like CLI assistant.")
    parser.add_argument("--model", help="Model name for the OpenAI-compatible API.")
    parser.add_argument("--api-key", help="API key. Defaults to TOYCLAW_API_KEY or OPENAI_API_KEY.")
    parser.add_argument("--api-url", help="API base URL or chat/completions endpoint.")
    parser.add_argument(
        "--print-context",
        action="store_true",
        help="Print the generated system prompt and exit.",
    )
    return parser.parse_args(list(argv))


def main(argv: Iterable[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    ensure_workspace()

    if args.print_context:
        print_context_preview()
        return 0

    config = build_config(args)
    client = ChatClient(config) if config else None
    print_banner(config)
    return repl(client)


if __name__ == "__main__":
    raise SystemExit(main())
