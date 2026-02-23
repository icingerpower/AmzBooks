#ifndef JOURNALENTRYFACTORY_H
#define JOURNALENTRYFACTORY_H

#include <QSharedPointer>
#include <QMultiMap>
#include <QDateTime>
#include "JournalEntry.h"
#include "PurchaseInvoiceManager.h"
#include <QCoroTask>
#include <functional>

class CurrencyRateManager;
class CompanyInfosTable;
class BooksAccountsSalesTable;
class BookAccountPurchaseTable;
class BookAccountSelfVatTable;
class JournalTable;
class ActivitySource;
class Shipment;
class AbstractBooksTableBank;
class BooksConnections;

class JournalEntryFactory
{
public:
    JournalEntryFactory(const CurrencyRateManager *currencyRateManager,
                        const CompanyInfosTable *companyInfos,
                        const BooksAccountsSalesTable *saleBookAccounts,
                        const BookAccountPurchaseTable *purchaseBookAccounts,
                        const JournalTable *journalTable,
                        const BookAccountSelfVatTable *selfVatBookAccounts = nullptr);

    // Create journal entry for purchase invoice
    // Negative amount is a refund
    QSharedPointer<JournalEntry> createEntry(PurchaseInformation purchaseInformation);

    // Create journal entry for shipment/sales activities
    // Each entry needs a French label very well detailed for a French accountant
    // Create journal entry for shipment/sales activities
    // Each entry needs a French label very well detailed for a French accountant
    QCoro::Task<QSharedPointer<JournalEntry>> createEntry(
            ActivitySource *source,
            const QMultiMap<QDateTime, QSharedPointer<Shipment>> &shipmentAndRefunds,
            std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing = nullptr);

    QCoro::Task<QSharedPointer<JournalEntry>> createEntry( // Create entry for one sale only with one shipment
            QSharedPointer<Shipment> shipmentOrRefund,
            std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing = nullptr);

    QSharedPointer<JournalEntry> createEntry(// Create entry for one sale only with one shipment
            const AbstractBooksTableBank *bankTable,
            const QString &nonBankAccount,
            int row);

private:
    const CurrencyRateManager *m_currencyRateManager;
    const CompanyInfosTable *m_companyInfos;
    const BooksAccountsSalesTable *m_saleBookAccounts;
    const BookAccountPurchaseTable *m_purchaseBookAccounts;
    const JournalTable *m_journalTable;
    const BookAccountSelfVatTable *m_selfVatBookAccounts;
};

#endif // JOURNALENTRYFACTORY_H
