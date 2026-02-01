#ifndef BANKPAYPAL_H
#define BANKPAYPAL_H

#include "AbstractBankStatement.h"

class BankPaypal : public AbstractBankStatement
{
public:
    BankPaypal();
    virtual ~BankPaypal() override = default;

    QStringList fileFilters() const override;
    QSharedPointer<QList<BankRow>> readRows(const QString &filePath) const override;
    
    virtual QString currency() const = 0;

protected:
    QString defaultJournal() const override;
};

#endif // BANKPAYPAL_H
