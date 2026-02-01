#ifndef BANKSTRIPEUSD_H
#define BANKSTRIPEUSD_H

#include "BankStripe.h"

class BankStripeUSD : public BankStripe
{
public:
    BankStripeUSD();
    QString getId() const override;
    QString getName() const override;
    QString defaultAccount() const override;
    QString defaultAccountFees() const override;
    QString currency() const override;
};

#endif // BANKSTRIPEUSD_H
