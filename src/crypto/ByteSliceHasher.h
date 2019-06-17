#pragma once

// Copyright 2018 EssaBlockChain Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ByteSlice.h"

namespace essa
{

// shortHash provides a fast and relatively secure *randomized* hash function
// this is suitable for keeping objects in memory but not for persisting objects
// or cryptographic use
namespace shortHash
{
void initialize();
uint64_t computeHash(essa::ByteSlice const& b);
}
}
