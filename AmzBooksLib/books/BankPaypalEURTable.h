#ifndef BANKPAYPALEURTABLE_H
#define BANKPAYPALEURTABLE_H

#include "AbstractBooksTableBank.h"
#include "banks/BankPaypalEUR.h"

class BankPaypalEURTable : public AbstractBooksTableBank
{
    Q_OBJECT
public:
    using AbstractBooksTableBank::AbstractBooksTableBank;
    
    QString getId() const override { return m_bank.getId(); } // "paypal_eur"
    const AbstractBankStatement *getBankStatement() const override { return &m_bank; }

private:
    BankPaypalEUR m_bank;
};

#endif // BANKPAYPALEURTABLE_H
