#ifndef JOURNALENTRYFACTORY_H
#define JOURNALENTRYFACTORY_H

#include <QSharedPointer>
#include <QMultiMap>
#include <QDateTime>
#include <QDate>
#include <QList>
#include <QHash>
#include <QCoroTask>
#include <functional>

#include "JournalEntry.h"
#include "PurchaseAmzPaymentsManager.h"
#include "PurchaseInvoiceManager.h"
#include "TaxScheme.h"

class CurrencyRateManager;
class CompanyInfosTable;
class BooksAccountsSalesTable;
class BookAccountPurchaseTable;
class BookAccountSelfVatTable;
class BookAccountAmzBalanceTable;
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
    // Per-activity detail row used in sale reports
    struct ShipmentReportInfo {
        QString store;             // marketplace store name (e.g. "amazon.fr")
        QDate date;
        QString orderId;           // activity.getEventId()
        QString shipmentRefundId;  // activity.getActivityId()
        bool isRefund = false;     // true when amount < 0
        double untaxedAmount = 0.0;
        double taxes = 0.0;
        double taxedAmount = 0.0;
        QString currency;
        double origTaxedAmount = 0.0;  // original total in source currency
        QString origCurrency;
        double vatRatePct = 0.0;       // VAT rate as a percentage (e.g. 20.0)
        TaxScheme taxScheme = TaxScheme::Unknown;
        QString countryFrom;
        QString countryTo;
        bool isCompany = false;   // true = B2B
        QString taxNumber;        // VAT/tax number for B2B, empty otherwise
    };

    // One group per unique (taxScheme, countryFrom, countryTo, vatRate, currency) combination
    struct GroupedShipmentData {
        TaxScheme taxScheme = TaxScheme::Unknown;
        QString countryFrom;
        QString countryTo;
        double vatRatePct = 0.0;
        QString currency;
        double totalRevenue = 0.0;  // sum of untaxed amounts
        double totalVat = 0.0;      // sum of tax amounts
        QString sampleEventId;      // sample order ID for error messages
        QString saleAccount;        // resolved sale account (empty if not looked up)
        QString vatAccount;         // resolved VAT account (empty if not looked up or 0%)
        QList<ShipmentReportInfo> shipments;
    };

    // Groups shipments by VAT key and collects per-activity report details.
    // orderId_store maps order IDs to store names (e.g. "amazon.fr"); pass {} if not needed.
    static QList<GroupedShipmentData> computeGrouping(
            ActivitySource *source,
            const QMultiMap<QDateTime, QSharedPointer<Shipment>> &shipmentAndRefunds,
            const QDate &entryDate,
            const QHash<QString, QString> &orderId_store = {});

    JournalEntryFactory(const CurrencyRateManager *currencyRateManager,
                        const CompanyInfosTable *companyInfos,
                        const BooksAccountsSalesTable *saleBookAccounts,
                        const BookAccountPurchaseTable *purchaseBookAccounts,
                        const JournalTable *journalTable,
                        const BookAccountSelfVatTable *selfVatBookAccounts,
                        const AmzPaymentSettings *amzPaymentSettings,
                        const BookAccountAmzBalanceTable *amzBalanceTable); // None can be nullptr in the UI production app

    // Create journal entry for purchase invoice
    // Negative amount is a refund
    QSharedPointer<JournalEntry> createEntry(const PurchaseInformation &purchaseInformation) const;
    QCoro::Task<QSharedPointer<JournalEntry>> createEntry(
        const AmzPaymentInfo &paymentInfo,
        std::function<QCoro::Task<bool>(const QString &, const QString &)> callbackAddIfMissing = nullptr) const;

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
    const BookAccountAmzBalanceTable *m_amzBalanceTable;
};

#endif // JOURNALENTRYFACTORY_H
