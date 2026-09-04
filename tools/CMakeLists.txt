add_subdirectory(catalog_validate)
add_subdirectory(assetc)
# PERF-001: ADR 0018 schema v1 harness (also builds with examples; flag is documented).
#
# Not admitted by TINA_BUILD_TOOLS: the benchmark harness is not part of the SDK, so an SDK-only
# configure has no reason to compile it. It keeps its own flag and the examples courtesy build.
if (TINA_BUILD_BENCHMARKS OR TINA_BUILD_EXAMPLES)
    add_subdirectory(bench)
endif ()
