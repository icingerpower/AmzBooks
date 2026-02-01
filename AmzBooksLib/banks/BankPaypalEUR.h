#ifndef BANKPAYPALEUR_H
#define BANKPAYPALEUR_H

#include "BankPaypal.h"

class BankPaypalEUR : public BankPaypal
{
public:
    BankPaypalEUR();
    QString getId() const override;
    QString getName() const override;
    QString defaultAccount() const override;
    QString defaultAccountFees() const override;
    QString currency() const override;
};

#endif // BANKPAYPALEUR_H
