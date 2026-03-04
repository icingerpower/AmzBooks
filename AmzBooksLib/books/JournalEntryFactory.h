#ifndef JOURNALENTRYFACTORY_H
#define JOURNALENTRYFACTORY_H

#include <QSharedPointer>
#include <QMultiMap>
#include <QDateTime>
#include <QCoroTask>
#include <functional>

#include "JournalEntry.h"
#include "PurchaseAmzPaymentsManager.h"
#include "PurchaseInvoiceManager.h"

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
class AmzPaymentSettings;
class InventoryMoveTree;

class JournalEntryFactory
{
public:
    JournalEntryFactory(const CurrencyRateManager *currencyRateManager,
                        const CompanyInfosTable *companyInfos,
                        const BooksAccountsSalesTable *saleBookAccounts,
                        const BookAccountPurchaseTable *purchaseBookAccounts,
                        const JournalTable *journalTable,
                        const BookAccountSelfVatTable *selfVatBookAccounts = nullptr,
                        const AmzPaymentSettings *amzPaymentSettings = nullptr);

    // Create journal entry for purchase invoice
    // Negative amount is a refund
    QSharedPointer<JournalEntry> createEntry(const PurchaseInformation &purchaseInformation) const;
    QSharedPointer<JournalEntry> createEntry(const AmzPaymentInfo &paymentInfo) const;

    // Create journal entry for shipment/sales activities
    // Each entry needs a French label very well detailed for a French accountant
    // Create journal entry for shipment/sales activities
    // Each entry needs a French label very well detailed for a French accountant
    QCoro::Task<QList<QSharedPointer<JournalEntry>>> createEntryGrouped(
            ActivitySource *source,
            const QMultiMap<QDateTime, QSharedPointer<Shipment>> &shipmentAndRefunds,
            std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing = nullptr) const;

    QCoro::Task<QSharedPointer<JournalEntry>> createEntry(// Create entry for one sale only with one shipment
        QSharedPointer<Shipment> shipmentOrRefund
        , const QString &customerAccount,
        std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing = nullptr) const;

    QSharedPointer<JournalEntry> createEntry(// Create entry for one sale only with one shipment
            const AbstractBooksTableBank *bankTable,
            const QString &nonBankAccount,
            int row) const;

    QSharedPointer<JournalEntry> createEntry(
        const InventoryMoveTree *inventoryMoveTree, const QString &countryCodeCompany) const;

private:
    const CurrencyRateManager *m_currencyRateManager;
    const CompanyInfosTable *m_companyInfos;
    const BooksAccountsSalesTable *m_saleBookAccounts;
    const BookAccountPurchaseTable *m_purchaseBookAccounts;
    const JournalTable *m_journalTable;
    const BookAccountSelfVatTable *m_selfVatBookAccounts;
    const AmzPaymentSettings *m_amzPaymentSettings;
};

#endif // JOURNALENTRYFACTORY_H
