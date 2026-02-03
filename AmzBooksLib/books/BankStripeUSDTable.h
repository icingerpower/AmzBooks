#ifndef BANKSTRIPEUSDTABLE_H
#define BANKSTRIPEUSDTABLE_H

#include "AbstractBooksTableBank.h"
#include "banks/BankStripeUSD.h"

class BankStripeUSDTable : public AbstractBooksTableBank
{
    Q_OBJECT
public:
    using AbstractBooksTableBank::AbstractBooksTableBank;
    
    QString getId() const override { return m_bank.getId(); }
    const AbstractBankStatement *getBankStatement() const override { return &m_bank; }

private:
    BankStripeUSD m_bank;
};

#endif // BANKSTRIPEUSDTABLE_H
