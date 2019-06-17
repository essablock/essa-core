#pragma once

// Copyright 2018 EssaBlockChain Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "crypto/ByteSliceHasher.h"
#include "xdr/EssaBlockChain-ledger.h"
#include <functional>

// implements a default hasher for "LedgerKey"
namespace std
{
template <> class hash<essa::LedgerKey>
{
  public:
    size_t
    operator()(essa::LedgerKey const& lk) const
    {
        size_t res;
        switch (lk.type())
        {
        case essa::ACCOUNT:
            res = essa::shortHash::computeHash(
                essa::ByteSlice(lk.account().accountID.ed25519().data(), 8));
            break;
        case essa::TRUSTLINE:
        {
            auto& tl = lk.trustLine();
            res = essa::shortHash::computeHash(
                essa::ByteSlice(tl.accountID.ed25519().data(), 8));
            switch (lk.trustLine().asset.type())
            {
            case essa::ASSET_TYPE_NATIVE:
                break;
            case essa::ASSET_TYPE_CREDIT_ALPHANUM4:
            {
                auto& tl4 = tl.asset.alphaNum4();
                res ^= essa::shortHash::computeHash(
                    essa::ByteSlice(tl4.issuer.ed25519().data(), 8));
                res ^= tl4.assetCode[0];
                break;
            }
            case essa::ASSET_TYPE_CREDIT_ALPHANUM12:
            {
                auto& tl12 = tl.asset.alphaNum12();
                res ^= essa::shortHash::computeHash(
                    essa::ByteSlice(tl12.issuer.ed25519().data(), 8));
                res ^= tl12.assetCode[0];
                break;
            }
            default:
                abort();
            }
            break;
        }
        case essa::DATA:
            res = essa::shortHash::computeHash(
                essa::ByteSlice(lk.data().accountID.ed25519().data(), 8));
            res ^= essa::shortHash::computeHash(essa::ByteSlice(
                lk.data().dataName.data(), lk.data().dataName.size()));
            break;
        case essa::OFFER:
            res = essa::shortHash::computeHash(essa::ByteSlice(
                &lk.offer().offerID, sizeof(lk.offer().offerID)));
            break;
        default:
            abort();
        }
        return res;
    }
};
}
