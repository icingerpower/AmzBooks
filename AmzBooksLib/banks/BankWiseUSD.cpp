#include "BankWiseUSD.h"

BankWiseUSD::BankWiseUSD()
{
}

QString BankWiseUSD::getId() const
{
    return "wise_usd";
}

QString BankWiseUSD::getName() const
{
    return "Wise USD";
}

QString BankWiseUSD::defaultAccount() const
{
    return "512700";
}

QString BankWiseUSD::defaultAccountFees() const
{
    return "627700";
}

QString BankWiseUSD::currency() const
{
    return "USD";
}

QStringList BankWiseUSD::fileFilters() const
{
    return QStringList() << "transferwise_USD_*.csv";
}

DECLARE_BANK_STATEMENT(BankWiseUSD)
