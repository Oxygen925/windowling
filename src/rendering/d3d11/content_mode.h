#pragma once

namespace dp {

// DComp 콘텐츠 방식 (P0 E3). P1 기본값은 SwapChain.
enum class ContentMode { SwapChain, Surface };

inline const char* ToString(ContentMode m) { return m == ContentMode::SwapChain ? "swapchain" : "surface"; }

}  // namespace dp
