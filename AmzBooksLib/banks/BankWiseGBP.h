#ifndef BANKWISH_GBP_H
#define BANKWISH_GBP_H

#include "BankWise.h"

class BankWiseGBP : public BankWise
{
public:
    BankWiseGBP();
    QString getId() const override;
    QString getName() const override;
    QString defaultAccount() const override;
    QString defaultAccountFees() const override;
    QString currency() const override;
    QStringList fileFilters() const override;
};

#endif // BANKWISH_GBP_H
