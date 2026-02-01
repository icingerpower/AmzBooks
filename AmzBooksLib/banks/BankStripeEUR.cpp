#include "BankStripeEUR.h"

BankStripeEUR::BankStripeEUR()
{
}

QString BankStripeEUR::getId() const
{
    return "stripe_eur";
}

QString BankStripeEUR::getName() const
{
    return "Stripe EUR";
}

QString BankStripeEUR::defaultAccount() const
{
    return "512400";
}

QString BankStripeEUR::defaultAccountFees() const
{
    return "627400";
}

QString BankStripeEUR::currency() const
{
    return "EUR";
}

DECLARE_BANK_STATEMENT(BankStripeEUR)
