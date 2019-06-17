#pragma once
#include "xdr/EssaBlockChain-ledger-entries.h"
#include "xdr/EssaBlockChain-ledger.h"
#include "xdr/EssaBlockChain-overlay.h"
#include "xdr/EssaBlockChain-transaction.h"
#include "xdr/EssaBlockChain-types.h"

namespace essa
{

std::string xdr_printer(const PublicKey& pk);
}
