// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/oracle_deviation.h>

#include <core_io.h> /// ValueFromAmount
#include <masternodes/masternodes.h> /// CCustomCSView
#include <rpc/util.h> /// AmountFromValue


Res ORACLE_DEVIATION::Import(const UniValue & val)
{
    deviation = AmountFromValue(val);
    return Res::Ok();
}

UniValue ORACLE_DEVIATION::Export() const
{
    return ValueFromAmount(deviation);
}

Res ORACLE_DEVIATION::Validate(const CCustomCSView & view) const
{
    verifyRes(view.GetLastHeight() >= Params().GetConsensus().FortCanningHeight, "Cannot be set before FortCanning");

    verifyRes(deviation >= COIN / 100, "Deviation cannot be less than 1 percent");

    return Res::Ok();
}

Res ORACLE_DEVIATION::Apply(CCustomCSView & mnview, uint32_t height)
{
    return mnview.SetPriceDeviation(deviation);
}
