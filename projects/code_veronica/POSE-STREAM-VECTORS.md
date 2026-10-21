# Vectorized pose history streams

The normal 60 FPS interpolation path enables two SSE2 operations:
- Pack four captured vertices (20-byte records) into twelve contiguous xyz floats with integer shuffles. UV/stamp data are not included in the output. Complete records only; scalar tail.
- Check stationary endpoints four floats at a time with the same float subtraction, absolute value and ordered > 1e-5f comparison as the original scalar loop. Complete loads only; scalar tail.

No temporal identities, matching candidates, ordering, 128-visit bounds, camera rules, alias vetoes, seam constraints or connected-component fallbacks are removed. No cache or extra allocation is introduced. Platforms without SSE2 use the scalar implementation.

The backend selects this only inside the existing opt-in 60 FPS interpolation initialization. Native 30 FPS remains on its existing path. CV_POSE_STREAM_VECTORS=0 restores the original scalar packing/stationary checks; otherwise the optimized path is enabled. Startup marker: [POSE-STREAM].

The user tested the experimental implementation in gameplay and reported it working correctly. No quantitative FPS gain is claimed. Production JP/US/EU use the same backend. No isolated tests were run for this change, per the user's request. Compilation was checked. Earlier uncommitted SPU, profiler, mesh-identity, position-comparison and unsuccessful caching experiments are outside this change.
