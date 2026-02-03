#ifndef BANKWISEEURTABLE_H
#define BANKWISEEURTABLE_H

#include "AbstractBooksTableBank.h"
#include "banks/BankWiseEUR.h"

class BankWiseEURTable : public AbstractBooksTableBank
{
    Q_OBJECT
public:
    using AbstractBooksTableBank::AbstractBooksTableBank;
    
    QString getId() const override { return m_bank.getId(); }
    const AbstractBankStatement *getBankStatement() const override { return &m_bank; }

private:
    BankWiseEUR m_bank;
};

#endif // BANKWISEEURTABLE_H
