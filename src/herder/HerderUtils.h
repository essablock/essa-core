#pragma once

// Copyright 2017 EssaBlockChain Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "xdr/EssaBlockChain-types.h"
#include <vector>

namespace essa
{

struct SCPEnvelope;
struct SCPStatement;
struct EssaBlockChainValue;

std::vector<Hash> getTxSetHashes(SCPEnvelope const& envelope);
std::vector<EssaBlockChainValue> getEssaBlockChainValues(SCPStatement const& envelope);
}