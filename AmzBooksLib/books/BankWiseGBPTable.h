#ifndef BANKWISEGBPTABLE_H
#define BANKWISEGBPTABLE_H

#include "AbstractBooksTableBank.h"
#include "banks/BankWiseGBP.h"

class BankWiseGBPTable : public AbstractBooksTableBank
{
    Q_OBJECT
public:
    using AbstractBooksTableBank::AbstractBooksTableBank;
    
    QString getId() const override { return m_bank.getId(); }
    const AbstractBankStatement *getBankStatement() const override { return &m_bank; }

private:
    BankWiseGBP m_bank;
};

#endif // BANKWISEGBPTABLE_H
