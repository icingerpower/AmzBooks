#include "JournalEntry.h"
#include <cmath>

JournalEntry::JournalEntry(const QDate &date, const QString &targetCurrency)
    : m_date(date), m_targetCurrency(targetCurrency)
{
}

void JournalEntry::addDebitLeft(const JournalEntry::EntryLine &entryLine, const QString &currency, double rate)
{
    EntryLine newLine = entryLine;
    
    // Store the original amount in the source currency
    double originalAmount = entryLine.currency_amount.value(currency, 0.0);
    newLine.currency_amount[currency] = originalAmount;
    
    // Convert to target currency if needed
    if (currency != m_targetCurrency) {
        double converted = originalAmount * rate;
        newLine.currency_amount[m_targetCurrency] = std::round(converted * 100.0) / 100.0;
        newLine.title = entryLine.title + QString(" (Conv: %1 %2 @ %3)")
                .arg(originalAmount)
                .arg(currency)
                .arg(rate);
    }
    
    m_entriesDebit.append(newLine);
}

void JournalEntry::addCreditRight(const JournalEntry::EntryLine &entryLine, const QString &currency, double rate)
{
    EntryLine newLine = entryLine;
    
    // Store the original amount in the source currency
    double originalAmount = entryLine.currency_amount.value(currency, 0.0);
    newLine.currency_amount[currency] = originalAmount;
    
    // Convert to target currency if needed
    if (currency != m_targetCurrency) {
        double converted = originalAmount * rate;
        newLine.currency_amount[m_targetCurrency] = std::round(converted * 100.0) / 100.0;
        newLine.title = entryLine.title + QString(" (Conv: %1 %2 @ %3)")
                .arg(originalAmount)
                .arg(currency)
                .arg(rate);
    }
    
    m_entriesCredit.append(newLine);
}

double JournalEntry::getDebitSum() const
{
    double sum = 0.0;
    for (const auto &entry : m_entriesDebit) {
        sum += entry.currency_amount.value(m_targetCurrency, 0.0);
    }
    return sum;
}

double JournalEntry::getCreditSum() const
{
    double sum = 0.0;
    for (const auto &entry : m_entriesCredit) {
        sum += entry.currency_amount.value(m_targetCurrency, 0.0);
    }
    return sum;
}

void JournalEntry::raiseExceptionIfDebitDifferentCredit()
{
    double debitSum = getDebitSum();
    double creditSum = getCreditSum();
    double diff = std::abs(debitSum - creditSum);
    
    // Round to 2 decimals to avoid floating point noise
    diff = std::round(diff * 100.0) / 100.0;
    
    if (diff >= 0.01) { // More than 1 cent difference
        throw ExceptionBookEquality(
            QString("Debit (%1) does not equal Credit (%2), difference: %3")
                .arg(debitSum)
                .arg(creditSum)
                .arg(debitSum - creditSum)
        );
    }
}

void JournalEntry::convertRoundingIfNeeded(double currencyRate, double maxRoundingDelta)
{
    // Since conversion is now done in add methods, this method mainly handles rounding differences
    double totalDebit = getDebitSum();
    double totalCredit = getCreditSum();
    
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
    roundingLine.currency_amount[m_targetCurrency] = diffAbs;

    if (diff > 0) {
        // Add to Credit
        m_entriesCredit.append(roundingLine);
    } else {
        // Add to Debit
        m_entriesDebit.append(roundingLine);
    }
}
