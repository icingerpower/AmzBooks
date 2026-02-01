#ifndef BANKWISE_H
#define BANKWISE_H

#include "AbstractBankStatement.h"

class BankWise : public AbstractBankStatement
{
public:
    BankWise();
    virtual ~BankWise() override = default;

    QStringList fileFilters() const override;
    QSharedPointer<QList<BankRow>> readRows(const QString &filePath) const override;
    
    virtual QString currency() const = 0;

protected:
    QString defaultJournal() const override;
};

#endif // BANKWISE_H
