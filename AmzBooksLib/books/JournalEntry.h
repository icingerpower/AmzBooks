#ifndef JOURNALENTRY_H
#define JOURNALENTRY_H

#include <QString>
#include <QVariant>
#include <QList>
#include <QDate>
#include "ExceptionBookEquality.h"

class JournalEntry
{
public:
    struct EntryLine{
        QString title;
        QString account;
        double amount;
    };
    JournalEntry(const QDate &date, const QString &targetCurrency);
    void addDebitLeft(const EntryLine &entryLine
                      , const QString &currency);
    void addCreditRight(const EntryLine &entryLine
                      , const QString &currency);
    void convertRoundingIfNeeded(double currencyRate
                                 , double maxRoundingDelta = 0.1); // Convert + round so sum debit = sum credit. If more than maxDelta, raise ExceptionBookEquality (to create). In title, add the convertion that was done (from which original amount, with which currency rate)

    const QList<EntryLine>& getDebits() const { return m_entriesDebit; }
    const QList<EntryLine>& getCredits() const { return m_entriesCredit; }

private:
    QDate m_date;
    QString m_targetCurrency;
    QList<EntryLine> m_entriesDebit;
    QList<EntryLine> m_entriesCredit;
    QList<double> m_origAmountsDebit;
    QList<double> m_origAmountsCredit;
    QList<QString> m_origCurrenciesDebit;
    QList<QString> m_origCurrenciesCredit; // Needed to track original currency for the "convertion that was done" requirement
};

#endif // JOURNALENTRY_H
