#ifndef BANKSTRIPEEURTABLE_H
#define BANKSTRIPEEURTABLE_H

#include "AbstractBooksTableBank.h"
#include "banks/BankStripeEUR.h"

class BankStripeEURTable : public AbstractBooksTableBank
{
    Q_OBJECT
public:
    using AbstractBooksTableBank::AbstractBooksTableBank;
    
    QString getId() const override { return m_bank.getId(); }
    const AbstractBankStatement *getBankStatement() const override { return &m_bank; }

private:
    BankStripeEUR m_bank;
};

#endif // BANKSTRIPEEURTABLE_H
