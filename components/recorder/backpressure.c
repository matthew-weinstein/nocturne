#include "backpressure.h"

bool backpressure_update(bool engaged, size_t occupancy, size_t capacity) {
    size_t occupancy_pct_scaled = occupancy * 100;

    if (occupancy_pct_scaled > capacity * BACKPRESSURE_ENGAGE_PCT) {
        return true;
    }
    if (occupancy_pct_scaled < capacity * BACKPRESSURE_RELEASE_PCT) {
        return false;
    }
    return engaged;
}
