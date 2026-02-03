#ifndef BANKQONTOTABLE_H
#define BANKQONTOTABLE_H

#include "AbstractBooksTableBank.h"
#include "banks/BankQonto.h"

class BankQontoTable : public AbstractBooksTableBank
{
    Q_OBJECT
public:
    using AbstractBooksTableBank::AbstractBooksTableBank;
    
    QString getId() const override { return m_bank.getId(); }
    const AbstractBankStatement *getBankStatement() const override { return &m_bank; }

private:
    BankQonto m_bank;
};

#endif // BANKQONTOTABLE_H
