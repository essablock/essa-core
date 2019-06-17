#include "HashOfHash.h"
#include "crypto/ByteSliceHasher.h"

namespace std
{

size_t
hash<essa::uint256>::operator()(essa::uint256 const& x) const noexcept
{
    size_t res =
        essa::shortHash::computeHash(essa::ByteSlice(x.data(), 8));

    return res;
}
}
