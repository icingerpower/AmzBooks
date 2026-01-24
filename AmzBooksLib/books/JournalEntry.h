#ifndef JOURNALENTRY_H
#define JOURNALENTRY_H

#include <QString>
#include <QVariant>
#include <QList>
#include <QHash>
#include <QDate>
#include "ExceptionBookEquality.h"

class JournalEntry
{
public:
    struct EntryLine{
        QString title;
        QString account;
        QHash<QString, double> currency_amount;
    };
    JournalEntry(const QDate &date, const QString &targetCurrency);
    void addDebitLeft(const EntryLine &entryLine
                      , const QString &currency, double rate = 1.0);
    void addCreditRight(const EntryLine &entryLine
                      , const QString &currency, double rate = 1.0);
    void convertRoundingIfNeeded(double currencyRate
                                 , double maxRoundingDelta = 0.1); // Convert + round so sum debit = sum credit. If more than maxDelta, raise ExceptionBookEquality (to create). In title, add the convertion that was done (from which original amount, with which currency rate)

    const QList<EntryLine>& getDebits() const { return m_entriesDebit; }
    const QList<EntryLine>& getCredits() const { return m_entriesCredit; }
    double getDebitSum() const;
    double getCreditSum() const;
    void raiseExceptionIfDebitDifferentCredit();

private:
    QDate m_date;
    QString m_targetCurrency;
    QList<EntryLine> m_entriesDebit;
    QList<EntryLine> m_entriesCredit;
};

#endif // JOURNALENTRY_H
