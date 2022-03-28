// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/accounts.h>
#include <masternodes/consensus/vaults.h>
#include <masternodes/govvariables/attributes.h>
#include <masternodes/loan.h>
#include <masternodes/masternodes.h>
#include <masternodes/mn_checks.h>
#include <masternodes/oracles.h>
#include <masternodes/tokens.h>
#include <masternodes/vault.h>

extern std::string ScriptToString(CScript const& script);

Res CVaultsConsensus::operator()(const CVaultMessage& obj) const {

    auto vaultCreationFee = consensus.vaultCreationFee;
    verifyRes(tx.vout[0].nValue == vaultCreationFee && tx.vout[0].nTokenId == DCT_ID{0},
              "Malformed tx vouts, creation vault fee is %s DFI", GetDecimaleString(vaultCreationFee));

    CVaultData vault{};
    static_cast<CVaultMessage&>(vault) = obj;

    // set loan scheme to default if non provided
    if (vault.schemeId.empty()) {
        auto defaultScheme = mnview.GetDefaultLoanScheme();
        verifyRes(defaultScheme, "There is no default loan scheme");
        vault.schemeId = *defaultScheme;
    }

    // loan scheme exists
    verifyRes(mnview.GetLoanScheme(vault.schemeId), "Cannot find existing loan scheme with id %s", vault.schemeId);

    // check loan scheme is not to be destroyed
    verifyRes(!mnview.GetDestroyLoanScheme(obj.schemeId), "Cannot set %s as loan scheme, set to be destroyed", obj.schemeId);

    return mnview.StoreVault(tx.GetHash(), vault);
}

Res CVaultsConsensus::operator()(const CCloseVaultMessage& obj) const {
    verifyRes(CheckCustomTx());

    // vault exists
    verifyDecl(vault, mnview.GetVault(obj.vaultId), "Vault <%s> not found", obj.vaultId.GetHex());

    // vault under liquidation
    verifyRes(!vault->isUnderLiquidation, "Cannot close vault under liquidation");

    // owner auth
    verifyRes(HasAuth(vault->ownerAddress), "tx must have at least one input from token owner");

    verifyRes(!mnview.GetLoanTokens(obj.vaultId), "Vault <%s> has loans", obj.vaultId.GetHex());

    CalculateOwnerRewards(obj.to);

    if (auto collaterals = mnview.GetVaultCollaterals(obj.vaultId))
        for (const auto& col : collaterals->balances)
            verifyRes(mnview.AddBalance(obj.to, {col.first, col.second}));

    // delete all interest to vault
    verifyRes(mnview.DeleteInterest(obj.vaultId, height));

    // return half fee, the rest is burned at creation
    auto feeBack = consensus.vaultCreationFee / 2;
    verifyRes(mnview.AddBalance(obj.to, {DCT_ID{0}, feeBack}));
    return mnview.EraseVault(obj.vaultId);
}

Res CVaultsConsensus::operator()(const CUpdateVaultMessage& obj) const {
    verifyRes(CheckCustomTx());

    // vault exists
    verifyDecl(vault, mnview.GetVault(obj.vaultId), "Vault <%s> not found", obj.vaultId.GetHex());

    // vault under liquidation
    verifyRes(!vault->isUnderLiquidation, "Cannot update vault under liquidation");

    // owner auth
    verifyRes(HasAuth(vault->ownerAddress), "tx must have at least one input from token owner");

    // loan scheme exists
    verifyRes(mnview.GetLoanScheme(obj.schemeId), "Cannot find existing loan scheme with id %s", obj.schemeId);

    // loan scheme is not set to be destroyed
    verifyRes(!mnview.GetDestroyLoanScheme(obj.schemeId), "Cannot set %s as loan scheme, set to be destroyed", obj.schemeId);

    verifyRes(IsVaultPriceValid(mnview, obj.vaultId, height), "Cannot update vault while any of the asset's price is invalid");

    // don't allow scheme change when vault is going to be in liquidation
    if (vault->schemeId != obj.schemeId)
        if (auto collaterals = mnview.GetVaultCollaterals(obj.vaultId)) {
            auto scheme = mnview.GetLoanScheme(obj.schemeId);
            for (int i = 0; i < 2; i++) {
                bool useNextPrice = i > 0, requireLivePrice = true;
                verifyRes(CheckCollateralRatio(obj.vaultId, *scheme, *collaterals, useNextPrice, requireLivePrice));
            }
        }

    vault->schemeId = obj.schemeId;
    vault->ownerAddress = obj.ownerAddress;
    return mnview.UpdateVault(obj.vaultId, *vault);
}

Res CVaultsConsensus::operator()(const CDepositToVaultMessage& obj) const {
    verifyRes(CheckCustomTx());

    // owner auth
    verifyRes(HasAuth(obj.from), "tx must have at least one input from token owner");

    // vault exists
    verifyDecl(vault, mnview.GetVault(obj.vaultId), "Vault <%s> not found", obj.vaultId.GetHex());

    // vault under liquidation
    verifyRes(!vault->isUnderLiquidation, "Cannot deposit to vault under liquidation");

    // If collateral token exist make sure it is enabled.
    if (mnview.GetCollateralTokenFromAttributes(obj.amount.nTokenId)) {
        CDataStructureV0 collateralKey{AttributeTypes::Token, obj.amount.nTokenId.v, TokenKeys::LoanCollateralEnabled};
        if (auto attributes = mnview.GetAttributes())
            verifyRes(attributes->GetValue(collateralKey, false), "Collateral token (%d) is disabled", obj.amount.nTokenId.v);
    }

    //check balance
    CalculateOwnerRewards(obj.from);
    verifyRes(mnview.SubBalance(obj.from, obj.amount), "Insufficient funds: can't subtract balance of %s: %s\n", ScriptToString(obj.from));

    verifyRes(mnview.AddVaultCollateral(obj.vaultId, obj.amount));

    bool useNextPrice = false, requireLivePrice = false;
    auto scheme = mnview.GetLoanScheme(vault->schemeId);
    auto collaterals = mnview.GetVaultCollaterals(obj.vaultId);
    return CheckCollateralRatio(obj.vaultId, *scheme, *collaterals, useNextPrice, requireLivePrice);
}

Res CVaultsConsensus::operator()(const CWithdrawFromVaultMessage& obj) const {
    verifyRes(CheckCustomTx());

    // vault exists
    verifyDecl(vault, mnview.GetVault(obj.vaultId), "Vault <%s> not found", obj.vaultId.GetHex());

    // vault under liquidation
    verifyRes(!vault->isUnderLiquidation, "Cannot withdraw from vault under liquidation");

    // owner auth
    verifyRes(HasAuth(vault->ownerAddress), "tx must have at least one input from token owner");

    verifyRes(IsVaultPriceValid(mnview, obj.vaultId, height), "Cannot withdraw from vault while any of the asset's price is invalid");

    verifyRes(mnview.SubVaultCollateral(obj.vaultId, obj.amount));

    if (!mnview.GetLoanTokens(obj.vaultId))
        return mnview.AddBalance(obj.to, obj.amount);

    verifyDecl(collaterals, mnview.GetVaultCollaterals(obj.vaultId),
              "Cannot withdraw all collaterals as there are still active loans in this vault");

    auto scheme = mnview.GetLoanScheme(vault->schemeId);
    verifyRes(CheckNextCollateralRatio(obj.vaultId, *scheme, *collaterals));
    return mnview.AddBalance(obj.to, obj.amount);
}

Res CVaultsConsensus::operator()(const CAuctionBidMessage& obj) const {
    verifyRes(CheckCustomTx());

    // owner auth
    verifyRes(HasAuth(obj.from), "tx must have at least one input from token owner");

    // vault exists
    verifyDecl(vault, mnview.GetVault(obj.vaultId), "Vault <%s> not found", obj.vaultId.GetHex());

    // vault under liquidation
    verifyRes(vault->isUnderLiquidation, "Cannot bid to vault which is not under liquidation");

    verifyDecl(data, mnview.GetAuction(obj.vaultId, height), "No auction data to vault %s", obj.vaultId.GetHex());

    verifyDecl(batch, mnview.GetAuctionBatch(obj.vaultId, obj.index),
               "No batch to vault/index %s/%d", obj.vaultId.GetHex(), obj.index);

    verifyRes(obj.amount.nTokenId == batch->loanAmount.nTokenId, "Bid token does not match auction one");

    auto bid = mnview.GetAuctionBid(obj.vaultId, obj.index);
    if (!bid) {
        auto amount = MultiplyAmounts(batch->loanAmount.nValue, COIN + data->liquidationPenalty);
        verifyRes(obj.amount.nValue >= amount, "First bid should include liquidation penalty of %d%%", data->liquidationPenalty * 100 / COIN);

        if (static_cast<int>(height) >= consensus.FortCanningMuseumHeight && data->liquidationPenalty)
            verifyRes(obj.amount.nValue > batch->loanAmount.nValue, "First bid should be higher than batch one");

    } else {
        auto amount = MultiplyAmounts(bid->second.nValue, COIN + (COIN / 100));
        verifyRes(obj.amount.nValue >= amount, "Bid override should be at least 1%% higher than current one");

        if (static_cast<int>(height) >= consensus.FortCanningMuseumHeight)
            verifyRes(obj.amount.nValue > bid->second.nValue, "Bid override should be higher than last one");

        // immediate refund previous bid
        CalculateOwnerRewards(bid->first);
        mnview.AddBalance(bid->first, bid->second);
    }
    //check balance
    CalculateOwnerRewards(obj.from);
    verifyRes(mnview.SubBalance(obj.from, obj.amount));
    return mnview.StoreAuctionBid(obj.vaultId, obj.index, {obj.from, obj.amount});
}
