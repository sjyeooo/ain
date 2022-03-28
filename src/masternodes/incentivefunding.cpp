// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/incentivefunding.h>

CAmount CCommunityBalancesView::GetCommunityBalance(CommunityAccountType account) const
{
    CAmount val;
    bool ok = ReadBy<ById>(static_cast<unsigned char>(account), val);
    if (ok) {
        return val;
    }
    return 0;
}

Res CCommunityBalancesView::SetCommunityBalance(CommunityAccountType account, CAmount amount)
{
    // deny negative values on db level!
    verifyRes(amount >= 0, "negative amount");
    WriteBy<ById>(static_cast<unsigned char>(account), amount);
    return Res::Ok();
}

void CCommunityBalancesView::ForEachCommunityBalance(std::function<bool (CommunityAccountType, CLazySerialize<CAmount>)> callback)
{
    ForEach<ById, unsigned char, CAmount>([&callback] (unsigned char key, CLazySerialize<CAmount> val) {
        return callback(CommunityAccountCodeToType(key), std::move(val));
    }, '\0');

}

Res CCommunityBalancesView::AddCommunityBalance(CommunityAccountType account, CAmount amount)
{
    if (amount == 0) {
        return Res::Ok();
    }
    verifyDecl(sum, SafeAdd(amount, GetCommunityBalance(account)));
    return SetCommunityBalance(account, sum);
}

Res CCommunityBalancesView::SubCommunityBalance(CommunityAccountType account, CAmount amount)
{
    if (amount == 0) {
        return Res::Ok();
    }
    verifyRes(amount > 0, "negative amount");
    CAmount oldBalance = GetCommunityBalance(account);
    verifyRes(oldBalance >= amount, "Amount %d is less than %d", oldBalance, amount);
    return SetCommunityBalance(account, oldBalance - amount);
}
