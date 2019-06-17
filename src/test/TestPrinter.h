#pragma once

// Copyright 2017 EssaBlockChain Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "catchup/CatchupWork.h"
#include "history/test/HistoryTestsUtils.h"
#include "lib/catch.hpp"
#include "xdrpp/printer.h"
#include "xdrpp/types.h"

namespace essa
{
struct OfferState;
}

namespace Catch
{
template <typename T>
struct StringMaker<T, typename std::enable_if<xdr::xdr_traits<T>::valid>::type>
{
    static std::string
    convert(T const& val)
    {
        return xdr::xdr_to_string(val);
    }
};

template <> struct StringMaker<essa::OfferState>
{
    static std::string convert(essa::OfferState const& os);
};

template <> struct StringMaker<essa::CatchupRange>
{
    static std::string convert(essa::CatchupRange const& cr);
};

template <> struct StringMaker<essa::historytestutils::CatchupPerformedWork>
{
    static std::string
    convert(essa::historytestutils::CatchupPerformedWork const& cr);
};
}
