# AGENTS.md

## General

Optimize for token and context efficiency without limiting the depth of analysis, exploration, debugging, implementation, or validation required to complete the task correctly.

Token efficiency means avoiding redundant or low-value work, not reducing necessary investigation.

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
- For long plans or work expected to span sessions, write the plan to `SESSION_HANDOFF.md` (repo root, gitignored) before executing, and execute from it.
- Update completion status in that file immediately after each step or sub-step is completed — never batch updates for the end of the session. A session can be cut off at any time; the file must never be more than one step stale.
- Keep updates terse: flip `[x]` / `[~]` / `[ ]` and, when a step ends mid-work, add one line stating exactly what remains in it.
- On session start, if `SESSION_HANDOFF.md` exists, read it first and resume from the first incomplete item without redoing completed work.
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