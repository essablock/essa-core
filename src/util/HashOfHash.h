#pragma once
#include <xdr/EssaBlockChain-types.h>

namespace std
{
template <> struct hash<essa::uint256>
{
    size_t operator()(essa::uint256 const& x) const noexcept;
};
}
