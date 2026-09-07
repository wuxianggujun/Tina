# Lifecycle Batch: 2026-09-06

## Scope

This is batch 1 of the requested implementation program. Runtime/Task/Asset ownership
changes are coded together, then built and tested together. No Editor or GPU product
gate is implied by the results recorded here.

## Current Contracts

- `EngineHost::stop(application)` ends a live external `start`/`tick` run. It uses
  `RunStopCause::ExplicitStop`, rejects another application or thread, and calls
  State exit and application shutdown exactly once. Lifecycle reentry is rejected.
- A Running/Starting/Stopping Host cannot be destroyed. Host hard shutdown reports
  a failed State-task deadline before terminating, without destroying worker owners.
- `StateTaskScope::cancelAndJoinFor` rejects invalid deadlines without cancellation;
  timeout retains the closed scope and its accepted tasks for retry. Its destructor
  uses the configured deadline instead of waiting indefinitely. The old unbounded
  `cancelAndJoin` entry point is removed.
- `TaskGroup` prepares its PMR-owned callable wrapper before publishing pending.
  Rejection/exception before scheduler acceptance rolls accounting back exactly once.
  Captured objects are destroyed before completion makes the group idle.
- `AssetLease` owns a reference to stable PMR storage, not an `AssetStore*`. CPU
  payloads and generation slots survive Store move/destruction until the last Lease.
  The memory resource must outlive the Store and every Lease. This can retain the
  entire Store allocation until its last Lease is released.
- Store/Lease payload queries are owner-thread-only; foreign-thread queries return
  no payload. Foreign-thread Lease release is a contract violation and terminates
  before touching the pool. This is not a cross-thread deferred-release queue.
- `AssetSystem::canMove()` makes the existing GPU-busy boundary inspectable. Move
  checks owner thread and live upload/retirement work before transferring any member.
  Binding registries and other borrowed facade references cannot span a move.
- GPU retirement keys include the exact resource generation. Independent registries
  can retire different GPU owners of the same CPU asset, including catalog rollback.
  Released diagnostic records retain identity, not ownership.
- Staging cancellation is validated and ledger storage reserved before backend
  acceptance. Backend rejection and pin allocation failure preserve staging tickets.
  Synchronous completion defers Lease release until local retirement commit finishes.
- glTF external source capture uses the same canonical root namespace as opened-file
  snapshots; Windows extended-length paths no longer fail lexical root comparison.

## Validation

The merged-source incremental Null build completed with exit code 0 for
`tina_tests` and `tina_asset_tests`, including their public-header isolation units.
No clean-first or temporary build tree was used. CMake regenerated the existing
tree; vcpkg reused the installed GTest dependency without adding Jolt/bgfx.

- `tina_asset_tests`: 397/397 passed, exit 0, no exclusions or skips. The previous
  seven failures and aborting catalog rollback case all passed. Four new regressions
  cover exact ledger identity, multiple GPU owners, duplicate retirement rejection,
  and staging preservation when pin allocation fails.
- `tina_tests` with the filter below: 84/84 passed, exit 0, no skips.
- `git diff --check`: passed. Modified Asset public headers contain no scanned
  third-party tokens.
- Editor, sample, real GPU, Physics3D, AI and mobile platform gates were not run.

```text
cmake --build --preset windows-vnext-debug --target tina_tests tina_asset_tests --parallel 1 -- /nr:false /p:CL_MPCount=1 /p:BuildInParallel=false
out/build/windows-msvc-vnext/bin/Debug/tina_asset_tests.exe --gtest_color=no
out/build/windows-msvc-vnext/bin/Debug/tina_tests.exe --gtest_color=no --gtest_filter=EngineHost*:TaskGroup*:StateTaskScope*:*TaskSystem*:*GameStateStack*
```

Local command evidence is retained by FastCtx jobs `j-gblgfh` (build), `j-jqeurs`
(Asset) and `j-zdcnow` (Runtime/Task).

## Resource Baseline

- Retained build tree: `out/build/windows-msvc-vnext`, 1,747,388,660 bytes before
  this build and 1,734,999,812 bytes after. It remains the core incremental tree;
  no manual deletion was performed.
- New temporary build-tree bytes: 0 before / 0 after. No container, volume,
  one-shot image, separate cache, helper, watchdog or window manager was created.
  Existing unrelated global containers/caches were not inspected or modified.
- Verified process state after the gate: MSBuild/cl/link/cmake and both test
  executables absent. No running FastCtx build/test job remains.
- The new retirement-test fixture directories are absent. Existing unrelated
  system-temp fixtures were not removed; their aggregate state is not claimed clean.
- Agent delegation was stopped at the user's request. The one attempted child
  failed before execution, made no file edits and has no running turn.
- Shared-workspace AI/Navigation/Water changes belong to the original parallel task.
- The user has integrated the Physics3D/floating-origin worktree into the main
  workspace. This task now proceeds serially, without agent delegation.
