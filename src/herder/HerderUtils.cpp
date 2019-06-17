// Copyright 2014 EssaBlockChain Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "herder/HerderUtils.h"
#include "scp/Slot.h"
#include "xdr/EssaBlockChain-ledger.h"
#include <algorithm>
#include <xdrpp/marshal.h>

namespace essa
{

std::vector<Hash>
getTxSetHashes(SCPEnvelope const& envelope)
{
    auto values = getEssaBlockChainValues(envelope.statement);
    auto result = std::vector<Hash>{};
    result.resize(values.size());

    std::transform(std::begin(values), std::end(values), std::begin(result),
                   [](EssaBlockChainValue const& sv) { return sv.txSetHash; });

    return result;
}

std::vector<EssaBlockChainValue>
getEssaBlockChainValues(SCPStatement const& statement)
{
    auto values = Slot::getStatementValues(statement);
    auto result = std::vector<EssaBlockChainValue>{};
    result.resize(values.size());

    std::transform(std::begin(values), std::end(values), std::begin(result),
                   [](Value const& v) {
                       auto wb = EssaBlockChainValue{};
                       xdr::xdr_from_opaque(v, wb);
                       return wb;
                   });

    return result;
}
}
