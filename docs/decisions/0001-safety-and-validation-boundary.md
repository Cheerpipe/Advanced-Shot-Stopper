# ADR 0001: Safety and validation boundary

- Status: accepted
- Date: 2026-09-07

## Context

Firmware changes range from documentation to direct control of an espresso
machine. A single undifferentiated gate either wastes resources or misses
safety evidence.

## Decision

Classify changed paths R0–R3, map unknown paths to R3, and permit only upward
overrides. Automated gates never operate hardware. Relay fail-open, safe boot,
timing/watchdog limits, disabled-by-default remote activation, resource
ownership, and image verification remain invariant. R3/release work records
required manual or HIL evidence explicitly.

## Consequences

Low-risk documentation gets a fast deterministic gate; critical paths receive
host sanitizers, both target builds, analysis, and human evidence. A passed
automated gate alone cannot declare an R3 image release-ready.
