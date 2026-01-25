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
class SaleBookAccountsTable;
class PurchaseBookAccountsTable;
class JournalTable;
class ActivitySource;
class Shipment;

class JournalEntryFactory
{
public:
    JournalEntryFactory(const CurrencyRateManager *currencyRateManager,
                        const CompanyInfosTable *companyInfos,
                        const SaleBookAccountsTable *saleBookAccounts,
                        const PurchaseBookAccountsTable *purchaseBookAccounts,
                        const JournalTable *journalTable);

    // Create journal entry for purchase invoice
    // Negative amount is a refund
    QSharedPointer<JournalEntry> createEntry(PurchaseInformation purchaseInformation);

    // Create journal entry for shipment/sales activities
    // Each entry needs a French label very well detailed for a French accountant
    // Create journal entry for shipment/sales activities
    // Each entry needs a French label very well detailed for a French accountant
    QCoro::Task<QSharedPointer<JournalEntry>> createEntry(ActivitySource *source,
                                             const QMultiMap<QDateTime, QSharedPointer<Shipment>> &shipmentAndRefunds,
                                             std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing = nullptr);

private:
    const CurrencyRateManager *m_currencyRateManager;
    const CompanyInfosTable *m_companyInfos;
    const SaleBookAccountsTable *m_saleBookAccounts;
    const PurchaseBookAccountsTable *m_purchaseBookAccounts;
    const JournalTable *m_journalTable;
};

#endif // JOURNALENTRYFACTORY_H
