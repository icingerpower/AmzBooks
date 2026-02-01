#ifndef BANKWISH_EUR_H
#define BANKWISH_EUR_H

#include "BankWise.h"

class BankWiseEUR : public BankWise
{
public:
    BankWiseEUR();
    QString getId() const override;
    QString getName() const override;
    QString defaultAccount() const override;
    QString defaultAccountFees() const override;
    QString currency() const override;
    QStringList fileFilters() const override;
};

#endif // BANKWISH_EUR_H
