#ifndef BANKWISH_USD_H
#define BANKWISH_USD_H

#include "BankWise.h"

class BankWiseUSD : public BankWise
{
public:
    BankWiseUSD();
    QString getId() const override;
    QString getName() const override;
    QString defaultAccount() const override;
    QString defaultAccountFees() const override;
    QString currency() const override;
    QStringList fileFilters() const override;
};

#endif // BANKWISH_USD_H
