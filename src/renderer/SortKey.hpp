//
// 提交排序键（64-bit）
// 参考 docs/render_architecture.md 的建议布局，便于桶内排序与提交
//

#pragma once

#include "../core/Core.hpp"

namespace Tina::RendererDetail {

// 64-bit sort key:
// [63..56] bucket/layer
// [55]     instanced flag
// [54..32] material/mesh key or reversed depth (具体由调用处组织)
// [31..0]  depth (非透明) 或者 instance/group id

inline Tina::u64 makeKey(Tina::u8 bucket, bool instanced, Tina::u32 hi, Tina::u32 lo) {
    Tina::u64 k = 0;
    k |= (Tina::u64(bucket) & 0xff) << 56;
    k |= (Tina::u64(instanced ? 1 : 0) & 0x1) << 55;
    k |= (Tina::u64(hi) & 0x1fffff) << 32; // 23 bits
    k |= (Tina::u64(lo) & 0xffffffffu);
    return k;
}

} // namespace Tina::RendererDetail

