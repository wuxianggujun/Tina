# ADR 0053: Retryable Host Shutdown

Status: Accepted

## Context

StateTaskScope already supports bounded joining, but Host used to pop State ownership before joining and terminate on timeout.
Startup and transition candidates could also die during stack unwinding with live captured work. TaskSystem joined only after
Render shutdown. These paths prevented an external driver from retaining owners and retrying a cooperative shutdown.

## Decision

- start/run/tick/stop share a Stopping state. Shutdown timeout retains the Host, application identity, candidate, committed
  states, scopes, UI and backends. The owner thread retries stop(); isStopping() exposes the outstanding lifetime obligation.
- Cancel all state generations before joining any. Each stop attempt shares one remaining timeout across scope and TaskSystem
  joins. Only joined workers permit candidate destruction, State onExit, application onShutdown and backend teardown.
- Retain the first stop cause and runtime error across retries. Failed candidates receive no onExit; uncommitted startup
  receives no application onShutdown. Successful callbacks happen once.
- The timeout bounds worker waits, not arbitrary user callbacks or Audio/Render shutdown. No detach, forced worker termination,
  hidden owner relocation or global shutdown registry is introduced.
- Destruction while Running/Stopping, wrong-thread destruction and pre-publication Create rollback remain fail-stop boundaries:
  there is no live caller-owned recovery handle after those operations begin.

This replaces the Host timeout-termination behavior described in the original Task shutdown contract. Memory policy remains
ADR 0052; bounded waits and queue backpressure are lifetime/load-control constraints, not a universal fixed-storage mandate.

## Verification

Runtime tests cover startup/committed/transition scope timeouts, cancellation of every state, capture destruction before State
destruction, generation stability, TaskSystem timeout from run/tick, retained backends, original stop cause and successful retry.
Existing wrong-thread, reentrant, Create rollback death and frame ownership failure tests remain required.
