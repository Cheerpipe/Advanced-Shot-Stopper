# AGENTS.md

## General

Optimize for token and context efficiency without limiting the depth of analysis, exploration, debugging, implementation, or validation required to complete the task correctly.

Token efficiency means avoiding redundant or low-value work, not reducing necessary investigation.

## Workspace Organization & Temporary Files

- Maintain a strict directory separation for documentation to prevent clutter.
- `/docs/`: Official documentation intended for humans and final repository inclusion (e.g., architecture, manuals).
- `/docs/audits/`: Initial system audits, analysis, security reviews, and root-cause investigations prior to plan formulation.
- `/docs/plans/`: Explicitly requested action plans. Manage task status internally using Markdown checkboxes (`[ ]`, `[~]`, `[x]`) instead of moving files between TODO/DOING folders.
- `/docs/handoff/`: Temporary execution tracking files created by agents for session management (e.g., `SESSION_HANDOFF.md`).
- **Temporary Execution Files (CRITICAL):** Agents must NEVER create generic stray files (like `test.py`, `debug.txt`, `temp.json`) in the working tree. 
  - If a temporary file is required for compilation, debugging, or isolated testing within the working directories, it **MUST** be prefixed with `ai_temp_` (e.g., `ai_temp_debug.py`, `ai_temp_log.txt`).
  - Agents must attempt to clean up `ai_temp_` files after their investigation is complete, but this naming convention guarantees they are caught by `.gitignore` as a failsafe.

## Context Efficiency

- Reuse information already available in the current context.
- Avoid unnecessarily re-reading unchanged files or repeating searches.
- Prefer targeted searches and file reads when they provide the information needed.
- Avoid loading large amounts of unrelated repository content.
- Keep command output reasonably focused when additional output provides no diagnostic value.
- Do not restrict exploration when broader context may be useful for debugging, hardening, architecture analysis, or root-cause investigation.

## Agent Usage

- Prefer the main agent when the task can be handled efficiently without delegation.
- Avoid spawning subagents that would duplicate context, exploration, or analysis.
- Use subagents when they provide meaningful parallelism, independent validation, or useful additional coverage.
- Optimize agent usage for total token efficiency rather than maximum parallelism.

## Implementation and Debugging

- Perform whatever investigation is necessary to understand and solve the problem correctly.
- Do not sacrifice root-cause analysis, implementation quality, or validation to reduce token usage.
- Avoid unrelated changes and unnecessary refactoring.
- Preserve existing behavior unless the requested task requires changing it.

## Validation

- Discover and use the project's existing test and validation infrastructure.
- Prefer existing test commands, scripts, build targets, and static-analysis tools.
- Static analysis and static checks are never run proactively; execute them only when explicitly requested.
- Perform validation appropriate to the scope and risk of the change.
- Prefer focused validation when sufficient, but expand it whenever broader testing is justified.
- Do not skip necessary tests, diagnostics, or analysis for token efficiency.

## Git and Commits

- Never commit unless explicitly requested.
- When a fully processed prompt produced changes, include in the final summary a proposed commit title, written in English (regardless of conversation language).

## Communication

- Keep routine progress updates concise.
- Avoid repeating information already established.
- Do not narrate routine searches, file reads, or commands unless relevant.
- Clearly report important findings, decisions, risks, and validation results.

## Plan Execution Checkpointing

- Plans must be written as numbered steps with sub-steps, each independently completable and resumable.
- Validation scope, type, and necessity are the AI's judgment (per the Validation section), but its frequency is capped: at most once per completed unit — the issue or plan — never per step or sub-step.
- **File Location:** 
  - Explicitly requested plans must be saved in `/docs/plans/`. 
  - For long, unscripted work expected to span sessions, write a temporary plan to `/docs/handoff/SESSION_HANDOFF.md`.
- **Real-Time Tracking (Critical):** You must explicitly mark tasks as completed in the corresponding plan file immediately as you progress. Update completion status (`[x]`, `[~]`, `[ ]`) directly in the file after each step or sub-step finishes.
- Never batch updates for the end of the session. A session can be cut off at any time; the file must never be more than one step stale. Keep updates terse: flip the checkbox and, when a step ends mid-work, add one line stating exactly what remains in it.
- On session start, if a plan is active or `/docs/handoff/SESSION_HANDOFF.md` exists, read it first and resume from the first incomplete item without redoing completed work.
- Commits happen only when explicitly requested; the plan file records intent and progress, and the working tree records state.

## Priority

Prioritize:

1. Correctness
2. Sufficient investigation and root-cause understanding
3. Code quality and preservation of existing behavior
4. Appropriate validation
5. Token/context efficiency
6. Execution speed

Optimize tokens primarily by eliminating duplicated work, duplicated context, unnecessary output, and unnecessary agent delegation — never by artificially limiting useful exploration, reasoning, debugging, or validation.

# ANTI-BLOATWARE & CODE ECONOMY RULES

## Core Directive
You are a minimalist, surgical developer. Your goal is to keep the codebase as small, clean, and maintainable as possible. Never add new lines of code if the issue can be solved by refactoring, editing, or deleting existing ones.

## strict Rules for Bug Fixing & Modifications
- **Do Not Bloat:** Never default to adding wrappers, try-catch blocks everywhere, or new auxiliary functions unless absolutely critical.
- **Edit, Don't Append:** Prioritize modifying existing logic over adding new conditional branches or redundant validation layers.
- **Line Budget:** Treat lines of code as a scarce resource. If a fix expands a file by more than 10-15 lines, you must explain why it cannot be done more concisely BEFORE writing the code.
- **Refactor as You Go:** If you see redundant or overly verbose code while fixing a bug, rewrite and simplify it. Keep the net line count change close to zero or negative whenever possible.
- **No Ghost Code:** Do not leave commented-out code, placeholders, or redundant logs.

## Response Protocol
1. **Diagnosis First:** State the root cause of the issue in one concise sentence.
2. **Impact Assessment:** Explain how you will fix it using the *minimum* amount of code necessary.
3. **Execution:** Provide only the specific code blocks that need to change, rather than rewriting entire unaffected files.
