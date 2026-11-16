#pragma once
#include <cstdint>
#include <algorithm>
namespace lb {
// Decode time is part of a GIF frame interval. Rebase after a slow frame instead
// of queueing a catch-up burst, which would starve input and show stale frames.
inline unsigned frame_wait(uint64_t now,uint64_t due,unsigned delay) noexcept {
    return due>now?unsigned(std::min<uint64_t>(due-now,delay)):1u;
}
inline uint64_t next_frame_due(uint64_t started,unsigned delay) noexcept {
    return started+std::max(10u,delay);
}
}
