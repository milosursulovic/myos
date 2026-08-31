---
name: myos-milestone
description: Start or advance a MyOS development milestone. Use when the user says "next milestone", "start milestone N", "implement UART/shell/GPIO/timer/scheduler/etc", or asks what milestone comes next. Enforces the incremental-development rule from CLAUDE.md — checks the previous milestone's Definition of Done before scaffolding the next one.
---

# MyOS milestone workflow

MyOS development is strictly incremental (`CLAUDE.md`, spec section 4 + 41).
Never start a milestone until the previous one is actually done.

1. Identify the current milestone from `README.md`'s roadmap checklist (or
   ask the user if it's ambiguous which one is "current").
2. Check the previous milestone's Definition of Done:
   - it compiles (`make` clean)
   - it flashed successfully
   - it was tested on real hardware — this one you must ask the user to
     confirm, you cannot verify it yourself
   - it has a short doc note under `docs/`
   If any of these is missing or unconfirmed, say so and don't proceed to
   scaffold the next milestone — offer to help close out the current one
   instead.
3. Once the previous milestone is confirmed done, scaffold the next one:
   - create/extend the relevant files under `boot/`, `kernel/`, `drivers/`,
     or `shell/` per the spec section for that milestone (delegate the
     actual implementation to the `avr-milestone-implementer` agent if the
     change is non-trivial)
   - add a short doc stub under `docs/` (name it after the component, e.g.
     `docs/uart.md`, matching the spec's suggested doc structure in section
     40) with: what it does, its API, how to test it manually
   - update the roadmap checkbox in `README.md` only once hardware-tested,
     not on scaffold
4. Never implement more than one milestone in the same pass. If the user
   asks for two at once, do the first, stop, and explain the next one is
   queued behind hardware verification of this one.
