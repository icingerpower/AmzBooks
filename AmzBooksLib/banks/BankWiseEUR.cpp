#include "BankWiseEUR.h"

BankWiseEUR::BankWiseEUR()
{
}

QString BankWiseEUR::getId() const
{
    return "wise_eur";
}

QString BankWiseEUR::getName() const
{
    return "Wise EUR";
}

QString BankWiseEUR::defaultAccount() const
{
    return "512600";
}

QString BankWiseEUR::defaultAccountFees() const
{
    return "627500";
}

QString BankWiseEUR::currency() const
{
    return "EUR";
}

QStringList BankWiseEUR::fileFilters() const
{
    return QStringList() << "transferwise_EUR_*.csv";
}

DECLARE_BANK_STATEMENT(BankWiseEUR)
