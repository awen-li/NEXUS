#ifndef NEXUS_DEMO_PROBE_H
#define NEXUS_DEMO_PROBE_H

#ifdef _WIN32
#  ifdef NEXUS_PROBE_BUILD
#    define NEXUS_PROBE_API __declspec(dllexport)
#  else
#    define NEXUS_PROBE_API __declspec(dllimport)
#  endif
#else
#  define NEXUS_PROBE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Stable C ABI for runtime adapters.
 *
 * A CPython extension, JNI agent, native library, or OS-side collector can emit
 * the same normalized boundary record without depending on the C++ analyzer.
 * Empty peer fields represent an object interaction without a known peer.
 */
NEXUS_PROBE_API int nexus_probe_init(const char *trace_path);

NEXUS_PROBE_API int nexus_probe_emit(
    const char *runtime_domain,
    const char *component,
    const char *mechanism,
    const char *role,
    const char *object_evidence,
    const char *provenance,
    const char *context,
    const char *resolution,
    const char *peer_runtime_domain,
    const char *peer_component);

NEXUS_PROBE_API int nexus_probe_flush(void);
NEXUS_PROBE_API void nexus_probe_close(void);
NEXUS_PROBE_API const char *nexus_probe_last_error(void);

#ifdef __cplusplus
}
#endif

#endif
