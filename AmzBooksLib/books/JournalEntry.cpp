#include "JournalEntry.h"
#include <cmath>

JournalEntry::JournalEntry(const QDate &date, const QString &targetCurrency)
    : m_date(date), m_targetCurrency(targetCurrency)
{
}

void JournalEntry::addDebitLeft(const JournalEntry::EntryLine &entryLine, const QString &currency)
{
    m_entriesDebit.append(entryLine);
    m_origAmountsDebit.append(entryLine.amount); // Initial amount is what's passed
    m_origCurrenciesDebit.append(currency);
}

void JournalEntry::addCreditRight(const JournalEntry::EntryLine &entryLine, const QString &currency)
{
    m_entriesCredit.append(entryLine);
    m_origAmountsCredit.append(entryLine.amount);
    m_origCurrenciesCredit.append(currency);
}

void JournalEntry::convertRoundingIfNeeded(double currencyRate, double maxRoundingDelta)
{
    double totalDebit = 0.0;
    double totalCredit = 0.0;
    
    // Process Debits
    for (int i = 0; i < m_entriesDebit.size(); ++i) {
        EntryLine &line = m_entriesDebit[i];
        QString origCurrency = m_origCurrenciesDebit[i];
        double origAmount = m_origAmountsDebit[i];

        if (origCurrency != m_targetCurrency) {
            double converted = origAmount * currencyRate;
            line.amount = std::round(converted * 100.0) / 100.0;
            line.title += QString(" (Conv: %1 %2 @ %3)").arg(origAmount).arg(origCurrency).arg(currencyRate);
        } else {
             line.amount = std::round(origAmount * 100.0) / 100.0;
        }
        totalDebit += line.amount;
    }

    // Process Credits
    for (int i = 0; i < m_entriesCredit.size(); ++i) {
        EntryLine &line = m_entriesCredit[i];
        QString origCurrency = m_origCurrenciesCredit[i];
        double origAmount = m_origAmountsCredit[i];

        if (origCurrency != m_targetCurrency) {
            double converted = origAmount * currencyRate;
            line.amount = std::round(converted * 100.0) / 100.0;
            line.title += QString(" (Conv: %1 %2 @ %3)").arg(origAmount).arg(origCurrency).arg(currencyRate);
        } else {
             line.amount = std::round(origAmount * 100.0) / 100.0;
        }
        totalCredit += line.amount;
    }

    double diff = totalDebit - totalCredit;
    double diffAbs = std::abs(diff);

    // Round diff to 2 decimals to avoid floating point noise issues when comparing to 0
    diffAbs = std::round(diffAbs * 100.0) / 100.0;

    if (diffAbs < 0.005) {
        // Balanced
        return;
    }

    if (diffAbs > maxRoundingDelta) {
        throw ExceptionBookEquality(QString("JournalEntry imbalance (%1) exceeds limit (%2)").arg(diff).arg(maxRoundingDelta));
    }

    // Add rounding entry
    // If diff > 0, Debit > Credit, we need more Credit.
    // If diff < 0, Credit > Debit, we need more Debit.
    // However, requirement says: "add a balancing entry... to the side with lower amount"
    // Which matches the above logic.

    EntryLine roundingLine;
    roundingLine.title = "Rounding difference";
    roundingLine.account = "ROUNDING"; // Or some default account? Using generic placeholder.
    roundingLine.amount = diffAbs;

    if (diff > 0) {
        // Add to Credit
        m_entriesCredit.append(roundingLine);
        // Trackers for symmetry, though not strictly used for further conversions
        m_origAmountsCredit.append(diffAbs);
        m_origCurrenciesCredit.append(m_targetCurrency);
    } else {
        // Add to Debit
        m_entriesDebit.append(roundingLine);
        m_origAmountsDebit.append(diffAbs);
        m_origCurrenciesDebit.append(m_targetCurrency);
    }
}
