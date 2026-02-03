#ifndef BANKWISEUSDTABLE_H
#define BANKWISEUSDTABLE_H

#include "AbstractBooksTableBank.h"
#include "banks/BankWiseUSD.h"

class BankWiseUSDTable : public AbstractBooksTableBank
{
    Q_OBJECT
public:
    using AbstractBooksTableBank::AbstractBooksTableBank;
    
    QString getId() const override { return m_bank.getId(); }
    const AbstractBankStatement *getBankStatement() const override { return &m_bank; }

private:
    BankWiseUSD m_bank;
};

#endif // BANKWISEUSDTABLE_H
