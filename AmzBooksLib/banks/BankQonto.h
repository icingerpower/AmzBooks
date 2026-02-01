#ifndef BANKQONTO_H
#define BANKQONTO_H

#include "AbstractBankStatement.h"

class BankQonto : public AbstractBankStatement
{
public:
    BankQonto();
    virtual ~BankQonto() override = default;

    QString getId() const override;
    QString getName() const override;
    QStringList fileFilters() const override;
    QString defaultAccount() const override;
    QString defaultAccountFees() const override;
    QString defaultJournal() const override;
    QSharedPointer<QList<BankRow>> readRows(const QString &filePath) const override;
};

#endif // BANKQONTO_H
