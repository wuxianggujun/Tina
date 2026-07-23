add_subdirectory(catalog_validate)
add_subdirectory(assetc)
# PERF-001: ADR 0018 schema v1 harness (also builds with examples; flag is documented).
if (TINA_BUILD_BENCHMARKS OR TINA_BUILD_EXAMPLES)
    add_subdirectory(bench)
endif ()
