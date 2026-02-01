#ifndef BANKSTRIPE_H
#define BANKSTRIPE_H

#include "AbstractBankStatement.h"

class BankStripe : public AbstractBankStatement
{
public:
    BankStripe();
    virtual ~BankStripe() override = default;

    QStringList fileFilters() const override;
    QSharedPointer<QList<BankRow>> readRows(const QString &filePath) const override;
    
    virtual QString currency() const = 0;

protected:
    QString defaultJournal() const override;
};

#endif // BANKSTRIPE_H
