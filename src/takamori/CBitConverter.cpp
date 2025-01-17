#include <bit>
#include <cstdint>

#include "acb_env.h"
#include "acb_env_ns.h"
#include "takamori/CBitConverter.h"

// http://stackoverflow.com/questions/2100331/c-macro-definition-to-determine-big-endian-or-little-endian-machine
enum {
    O32_LITTLE_ENDIAN = 0x03020100ul,
    O32_BIG_ENDIAN    = 0x00010203ul,
    O32_PDP_ENDIAN    = 0x01000302ul
};

static const union {
    std::uint8_t bytes[4]; // NOLINT(modernize-avoid-c-arrays)
    std::uint32_t value;
} _o32_host_order = {
    {0, 1, 2, 3}
};

#define O32_HOST_ORDER (_o32_host_order.value)

ACB_NS_BEGIN

auto CBitConverter::IsLittleEndian() -> bool_t {
    return static_cast<bool_t>(O32_HOST_ORDER == O32_LITTLE_ENDIAN);
}

auto CBitConverter::ToSingle(const void *p) -> float {
    return std::bit_cast<float>(ToInt32(p));
}

auto CBitConverter::ToDouble(const void *p) -> double {
    return std::bit_cast<double>(ToInt64(p));
}

#define TO_INT(bit, u, U)                                                      \
    auto CBitConverter::To##U##Int##bit(const void *p)->std::u##int##bit##_t { \
        const auto *pb    = static_cast<const std::uint8_t *>(p);              \
        u##int##bit##_t v = 0;                                                 \
        if (IsLittleEndian()) {                                                \
            for (auto i = 0; i < ((bit) / 8); ++i) {                           \
                v = v | (pb[i] << (i * 8));                                    \
            }                                                                  \
        } else {                                                               \
            for (auto i = 0; i < ((bit) / 8); ++i) {                           \
                v = (v << 8) | pb[i];                                          \
            }                                                                  \
        };                                                                     \
        return v;                                                              \
    }

TO_INT(16, , )

TO_INT(16, u, U)

TO_INT(32, , )

TO_INT(32, u, U)

TO_INT(64, , )

TO_INT(64, u, U)

ACB_NS_END
