#include "BankStripeUSD.h"

BankStripeUSD::BankStripeUSD()
{
}

QString BankStripeUSD::getId() const
{
    return "stripe_usd";
}

QString BankStripeUSD::getName() const
{
    return "Stripe USD";
}

QString BankStripeUSD::defaultAccount() const
{
    return "512401";
}

QString BankStripeUSD::defaultAccountFees() const
{
    return "627400";
}

QString BankStripeUSD::currency() const
{
    return "USD";
}

DECLARE_BANK_STATEMENT(BankStripeUSD)
