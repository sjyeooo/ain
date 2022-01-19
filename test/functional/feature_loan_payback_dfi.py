#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - payback loan."""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

import calendar
import time
from decimal import Decimal


class PaybackLoanTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1',
                '-fortcanningheight=50', '-fortcanninghillheight=50', '-eunosheight=50', '-txindex=1']
        ]

    def run_test(self):
        self.nodes[0].generate(150)

        account0 = self.nodes[0].get_genesis_keys().ownerAuthAddress

        symbolDFI = "DFI"
        symbolBTC = "BTC"
        symboldUSD = "DUSD"
        symbolTSLA = "TSLA"

        self.nodes[0].createtoken({
            "symbol": symbolBTC,
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": account0
        })

        self.nodes[0].generate(1)

        idDFI = list(self.nodes[0].gettoken(symbolDFI).keys())[0]
        idBTC = list(self.nodes[0].gettoken(symbolBTC).keys())[0]

        self.nodes[0].utxostoaccount({account0: "1000@" + symbolDFI})

        oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds1 = [
            {"currency": "USD", "token": "DFI"},
            {"currency": "USD", "token": "BTC"},
            {"currency": "USD", "token": "TSLA"}
        ]
        oracle_id1 = self.nodes[0].appointoracle(
            oracle_address1, price_feeds1, 10)
        self.nodes[0].generate(1)

        # feed oracle
        oracle1_prices = [
            {"currency": "USD", "tokenAmount": "10@TSLA"},
            {"currency": "USD", "tokenAmount": "10@DFI"},
            {"currency": "USD", "tokenAmount": "10@BTC"}
        ]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(1)

        self.nodes[0].setcollateraltoken({
            'token': idDFI,
            'factor': 1,
            'fixedIntervalPriceId': "DFI/USD"
        })

        self.nodes[0].setcollateraltoken({
            'token': idBTC,
            'factor': 1,
            'fixedIntervalPriceId': "BTC/USD"})

        self.nodes[0].generate(1)

        self.nodes[0].setloantoken({
            'symbol': symbolTSLA,
            'name': "Tesla stock token",
            'fixedIntervalPriceId': "TSLA/USD",
            'mintable': True,
            'interest': 1
        })

        self.nodes[0].setloantoken({
            'symbol': symboldUSD,
            'name': "DUSD stable token",
            'fixedIntervalPriceId': "DUSD/USD",
                                    'mintable': True,
                                    'interest': 1
        })
        self.nodes[0].generate(1)

        self.nodes[0].createloanscheme(150, 5, 'LOAN150')

        self.nodes[0].generate(5)

        iddUSD = list(self.nodes[0].gettoken(symboldUSD).keys())[0]

        vaultId = self.nodes[0].createvault(account0, 'LOAN150')
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(vaultId, account0, "400@DFI")
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': "2000@" + symboldUSD
        })
        self.nodes[0].generate(1)

        poolOwner = self.nodes[0].getnewaddress("", "legacy")
        # create pool DUSD-DFI
        self.nodes[0].createpoolpair({
            "tokenA": iddUSD,
            "tokenB": idDFI,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DUSD-DFI",
        })
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity(
            {account0: ["30@" + symbolDFI, "300@" + symboldUSD]}, account0)
        self.nodes[0].generate(1)

        # Should not be able to payback loan with BTC
        assert_raises_rpc_error(-32600, "Loan token with id (1) does not exist!", self.nodes[0].paybackloan, {
            'vaultId': vaultId,
            'from': account0,
            'amounts': "1@BTC"
        })

        # Should not be able to payback loan before DFI payback enabled
        assert_raises_rpc_error(-32600, "Payback of DUSD loans with DFI not currently active", self.nodes[0].paybackloan, {
            'vaultId': vaultId,
            'from': account0,
            'amounts': "1@DFI"
        })

        # Enable loan payback
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + iddUSD + '/payback_dfi':'true'}})
        self.nodes[0].generate(1)

        vaultBefore = self.nodes[0].getvault(vaultId)
        [amountBefore, _] = vaultBefore['loanAmounts'][0].split('@')

        # Partial loan payback in DFI
        self.nodes[0].paybackloan({
            'vaultId': vaultId,
            'from': account0,
            'amounts': "1@DFI"
        })
        self.nodes[0].generate(1)

        vaultAfter = self.nodes[0].getvault(vaultId)
        print(vaultAfter)
        [amountAfter, _] = vaultAfter['loanAmounts'][0].split('@')
        [interestAfter, _] = vaultAfter['interestAmounts'][0].split('@')

        assert_equal(Decimal(amountAfter) - Decimal(interestAfter), (Decimal(amountBefore) - (10 * Decimal('0.99'))))

        # Payback of loan token other than DUSD
        vaultId2 = self.nodes[0].createvault(account0, 'LOAN150')
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(vaultId2, account0, "100@DFI")
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({
            'vaultId': vaultId2,
            'amounts': "10@" + symbolTSLA
        })
        self.nodes[0].generate(1)

        # Should not be able to payback loan token other than DUSD with DFI
        assert_raises_rpc_error(-32600, "There is no loan on token (DUSD) in this vault!", self.nodes[0].paybackloan, {
            'vaultId': vaultId2,
            'from': account0,
            'amounts': "10@DFI"
        })


if __name__ == '__main__':
    PaybackLoanTest().main()
