#include "BankPaypalUSD.h"

BankPaypalUSD::BankPaypalUSD()
{
}

QString BankPaypalUSD::getId() const
{
    return "paypal_usd";
}

QString BankPaypalUSD::getName() const
{
    return "Paypal USD";
}

QString BankPaypalUSD::defaultAccount() const
{
    return "467000";
}

QString BankPaypalUSD::defaultAccountFees() const
{
    return "627100";
}

QString BankPaypalUSD::currency() const
{
    return "USD";
}

DECLARE_BANK_STATEMENT(BankPaypalUSD)
