# Prefer the system nlohmann/json package

The root CMake build first calls `find_package(nlohmann_json CONFIG QUIET)` and uses an installed package such as `nlohmann-json3-dev` when available. If it is missing and `JEV_FETCH_NLOHMANN_JSON` is explicitly enabled, CMake fetches the pinned v3.11.3 tarball from GitHub; otherwise it repeats the lookup as `REQUIRED` and stops with a clear configuration error.

**Status:** accepted

**Consequences:** Normal builds stay offline and use the host package. Network access is an explicit opt-in, and the trace/config tooling has a reproducible versioned fallback.
