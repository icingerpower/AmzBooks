#ifndef BANKPAYPALUSD_H
#define BANKPAYPALUSD_H

#include "BankPaypal.h"

class BankPaypalUSD : public BankPaypal
{
public:
    BankPaypalUSD();
    QString getId() const override;
    QString getName() const override;
    QString defaultAccount() const override;
    QString defaultAccountFees() const override;
    QString currency() const override;
};

#endif // BANKPAYPALUSD_H
