/**
 * RingBuffer.cpp - Lock-free SPSC implementation
 * Note: Most logic is in header (template class)
 */

#include "rtv/audio/RingBuffer.hpp"

namespace rtv::audio {

// Explicit instantiation for common types
template class RingBuffer<float>;
template class RingBuffer<int16_t>;

} // namespace rtv::audio
