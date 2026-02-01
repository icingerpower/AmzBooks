#include "BankWiseGBP.h"

BankWiseGBP::BankWiseGBP()
{
}

QString BankWiseGBP::getId() const
{
    return "wise_gbp";
}

QString BankWiseGBP::getName() const
{
    return "Wise GBP";
}

QString BankWiseGBP::defaultAccount() const
{
    return "512800";
}

QString BankWiseGBP::defaultAccountFees() const
{
    return "627600";
}

QString BankWiseGBP::currency() const
{
    return "GBP";
}

QStringList BankWiseGBP::fileFilters() const
{
    return QStringList() << "transferwise_GBP_*.csv";
}

DECLARE_BANK_STATEMENT(BankWiseGBP)
