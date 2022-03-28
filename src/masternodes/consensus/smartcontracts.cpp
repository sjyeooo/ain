// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <masternodes/accounts.h>
#include <masternodes/consensus/smartcontracts.h>
#include <masternodes/govvariables/attributes.h>
#include <masternodes/gv.h>
#include <masternodes/masternodes.h>
#include <masternodes/tokens.h>

Res CSmartContractsConsensus::HandleDFIP2201Contract(const CSmartContractMessage& obj) const {
    verifyDecl(attributes, mnview.GetAttributes(), "Attributes unavailable");

    CDataStructureV0 activeKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIP2201Keys::Active};

    verifyRes(attributes->GetValue(activeKey, false), "DFIP2201 smart contract is not enabled");

    verifyRes(obj.name == SMART_CONTRACT_DFIP_2201, "DFIP2201 contract mismatch - got: " + obj.name);

    verifyRes(obj.accounts.size() == 1, "Only one address entry expected for " + obj.name);

    verifyRes(obj.accounts.begin()->second.balances.size() == 1, "Only one amount entry expected for " + obj.name);

    const auto& script = obj.accounts.begin()->first;
    verifyRes(HasAuth(script), "Must have at least one input from supplied address");

    const auto& id = obj.accounts.begin()->second.balances.begin()->first;
    const auto& amount = obj.accounts.begin()->second.balances.begin()->second;

    verifyRes(amount > 0, "Amount out of range");

    CDataStructureV0 minSwapKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIP2201Keys::MinSwap};
    auto minSwap = attributes->GetValue(minSwapKey, CAmount{0});

    verifyRes(amount >= minSwap, "Below minimum swapable amount, must be at least " + GetDecimaleString(minSwap) + " BTC");

    verifyDecl(token, mnview.GetToken(id), "Specified token not found");

    verifyRes(token->symbol == "BTC" && token->name == "Bitcoin" && token->IsDAT(), "Only Bitcoin can be swapped in " + obj.name);

    verifyRes(mnview.SubBalance(script, {id, amount}));

    const CTokenCurrencyPair btcUsd{"BTC","USD"};
    const CTokenCurrencyPair dfiUsd{"DFI","USD"};

    bool useNextPrice{false}, requireLivePrice{true};
    verifyDecl(BtcUsd, mnview.GetValidatedIntervalPrice(btcUsd, useNextPrice, requireLivePrice));

    CDataStructureV0 premiumKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIP2201Keys::Premium};
    auto premium = attributes->GetValue(premiumKey, CAmount{2500000});

    const auto& btcPrice = MultiplyAmounts(*BtcUsd, premium + COIN);

    verifyDecl(DfiUsd, mnview.GetValidatedIntervalPrice(dfiUsd, useNextPrice, requireLivePrice));

    const auto totalDFI = MultiplyAmounts(DivideAmounts(btcPrice, *DfiUsd), amount);
    verifyRes(mnview.SubBalance(consensus.smartContracts.begin()->second, {{0}, totalDFI}));
    return mnview.AddBalance(script, {{0}, totalDFI});
}

Res CSmartContractsConsensus::operator()(const CSmartContractMessage& obj) const {
    verifyRes(!obj.accounts.empty(), "Contract account parameters missing");

    auto contracts = consensus.smartContracts;
    auto contract = contracts.find(obj.name);
    verifyRes(contract != contracts.end(), "Specified smart contract not found");

    // Convert to switch when it's long enough.
    if (obj.name == SMART_CONTRACT_DFIP_2201)
        return HandleDFIP2201Contract(obj);

    return Res::Err("Specified smart contract not found");
}
