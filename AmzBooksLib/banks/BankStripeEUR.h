#ifndef BANKSTRIPEEUR_H
#define BANKSTRIPEEUR_H

#include "BankStripe.h"

class BankStripeEUR : public BankStripe
{
public:
    BankStripeEUR();
    QString getId() const override;
    QString getName() const override;
    QString defaultAccount() const override;
    QString defaultAccountFees() const override;
    QString currency() const override;
};

#endif // BANKSTRIPEEUR_H
