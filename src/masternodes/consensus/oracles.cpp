// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/params.h>
#include <masternodes/consensus/oracles.h>
#include <masternodes/masternodes.h>
#include <masternodes/oracles.h>

Res COraclesConsensus::operator()(const CAppointOracleMessage& obj) const {
    verifyRes(HasFoundationAuth(), "tx not from foundation member");

    COracle oracle;
    static_cast<CAppointOracleMessage&>(oracle) = obj;
    verifyRes(NormalizeTokenCurrencyPair(oracle.availablePairs));
    return mnview.AppointOracle(tx.GetHash(), oracle);
}

Res COraclesConsensus::operator()(const CUpdateOracleAppointMessage& obj) const {
    verifyRes(HasFoundationAuth(), "tx not from foundation member");

    COracle oracle;
    static_cast<CAppointOracleMessage&>(oracle) = obj.newOracleAppoint;
    verifyRes(NormalizeTokenCurrencyPair(oracle.availablePairs));
    return mnview.UpdateOracle(obj.oracleId, std::move(oracle));
}

Res COraclesConsensus::operator()(const CRemoveOracleAppointMessage& obj) const {
    verifyRes(HasFoundationAuth(), "tx not from foundation member");
    return mnview.RemoveOracle(obj.oracleId);
}

Res COraclesConsensus::operator()(const CSetOracleDataMessage& obj) const {
    verifyDecl(oracle, mnview.GetOracleData(obj.oracleId),
              "failed to retrieve oracle <%s> from database", obj.oracleId.GetHex());

    verifyRes(HasAuth(oracle.val->oracleAddress), "tx must have at least one input from account owner");

    if (height >= uint32_t(consensus.FortCanningHeight)) {
        for (const auto& tokenPrice : obj.tokenPrices)
            for (const auto& price : tokenPrice.second) {
                verifyRes(price.second > 0, "Amount out of range");

                auto timestamp = time;
                extern bool diffInHour(int64_t time1, int64_t time2);
                verifyRes(diffInHour(obj.timestamp, timestamp), "Timestamp (%d) is out of price update window (median: %d)",
                                     obj.timestamp, timestamp);
            }
    }
    return mnview.SetOracleData(obj.oracleId, obj.timestamp, obj.tokenPrices);
}
