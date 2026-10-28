#pragma once
#ifdef __cplusplus
extern "C" {
#endif
/* Optional title-owned filters. A NULL callback preserves normal pad input. */
void cellpad_gameplay_filters_set(int (*key)(int),unsigned (*buttons)(unsigned),int (*right_stick)(void));
#ifdef __cplusplus
}
#endif
