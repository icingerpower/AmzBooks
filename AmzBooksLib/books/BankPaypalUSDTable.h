#ifndef BANKPAYPALUSDTABLE_H
#define BANKPAYPALUSDTABLE_H

#include "AbstractBooksTableBank.h"
#include "banks/BankPaypalUSD.h"

class BankPaypalUSDTable : public AbstractBooksTableBank
{
    Q_OBJECT
public:
    using AbstractBooksTableBank::AbstractBooksTableBank;
    
    QString getId() const override { return m_bank.getId(); }
    const AbstractBankStatement *getBankStatement() const override { return &m_bank; }

private:
    BankPaypalUSD m_bank;
};

#endif // BANKPAYPALUSDTABLE_H
