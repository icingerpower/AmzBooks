#include "BankPaypalEUR.h"

BankPaypalEUR::BankPaypalEUR()
{
}

QString BankPaypalEUR::getId() const
{
    return "paypal_eur";
}

QString BankPaypalEUR::getName() const
{
    return "Paypal EUR";
}

QString BankPaypalEUR::defaultAccount() const
{
    return "467000";
}

QString BankPaypalEUR::defaultAccountFees() const
{
    return "627100";
}

QString BankPaypalEUR::currency() const
{
    return "EUR";
}

DECLARE_BANK_STATEMENT(BankPaypalEUR)
