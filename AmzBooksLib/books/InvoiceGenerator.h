#ifndef INVOICEGENERATOR_H
#define INVOICEGENERATOR_H

#include <QAbstractTableModel>
#include <QDir>
#include <QDate>
#include <QHash>

#include "TaxResolver.h"
#include "TaxScheme.h"
#include "TaxJurisdictionLevel.h"

class CompanyInfosTable;
class CompanyAddressTable;
class CurrencyRateManager;
class VatNumbersTable;
class Address;
class InvoicingInfo;

#include "orders/OrderManager.h"

class InvoiceGenerator : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit InvoiceGenerator(
        const QDir &workingDir,
        const CompanyInfosTable *companyInfos,
        const CompanyAddressTable *companyAddress,
        const CurrencyRateManager *currencyRates,
        const VatNumbersTable *vatNumbers = nullptr,
        QObject *parent = nullptr);

    // Invoice number generation
    QString getBaseInvoiceNumber(
        const QDate &date,
        const TaxResolver::TaxContext &taxContext,
        const QString &channel,
        const QString &store,
        const QString &shipmentId);

    QStringList getNextInvoiceNumbers(
        const QDate &date,
        const TaxResolver::TaxContext &taxContext,
        const QString &channel,
        const QString &store,
        const QList<bool> &invoicesToDo,
        const std::optional<QString> &existingInvoiceNumber,
        const QStringList &shipmentIds,
        const OrderManager *orderManager = nullptr, // When provided, discovers sale invoices
                                                    // stored in OrderManager but not yet in
                                                    // this generator's own registry, so that
                                                    // refunds for those orders receive -Rxx
                                                    // suffixes instead of new base numbers.
        const QStringList &activityIds = {},  // Per-entry activity IDs (e.g. "3105_refund").
                                              // When provided, revision records store the
                                              // activityId so that regenerateInvoices can
                                              // retrieve the correct invoicingInfo for refunds.
        const QList<QDate> &perEntryDates = {}); // Per-entry dates for new base invoice numbers.
                                                 // When provided, each new base invoice uses
                                                 // perEntryDates[i] instead of `date`, so orders
                                                 // from different months get the correct YYYYMM
                                                 // prefix even when grouped under the same context.

    void generateInvoice(
        const QString &invoiceNumber,
        const QString &previousInvoiceNumber,
        const QString &destinationPath,
        const Address &addressTo,
        const InvoicingInfo &invoicingInfo,
        const QString &orderId,
        OrderManager &orderManager,
        const QDate &invoiceDate = QDate(),
        const QString &shipmentId = QString()); // When provided, invoicing info is recorded
                                               // under shipmentId instead of orderId, so that
                                               // subsequent lookups via the shipment root ID
                                               // work correctly (avoids writing under the
                                               // Amazon order ID which is not a shipment root).

    // Regenerate PDFs for all invoice records whose date falls within [dateFrom, dateTo].
    // Retrieves InvoicingInfo and address from orderManager via the stored shipmentId.
    void regenerateInvoices(
        const QDir &folderTo,
        const QDate &dateFrom,
        const QDate &dateTo,
        OrderManager &orderManager);

    // QAbstractTableModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    // Static shortcut maps - easy to maintain, add new values here
    static const QHash<TaxScheme, QString> TAX_SCHEME_SHORTCUTS;
    static const QHash<TaxJurisdictionLevel, QString> TAX_JURISDICTION_SHORTCUTS;
    static const QHash<QString, QString> CHANNEL_SHORTCUTS;
    static const QHash<QString, QString> STORE_SHORTCUTS;

    // Shortcut helper methods
    static QString shortenTaxScheme(TaxScheme scheme);
    static QString shortenTaxJurisdiction(TaxJurisdictionLevel level);
    static QString shortenChannel(const QString &channel);
    static QString shortenStore(const QString &store);

    // Column indices
    enum Column {
        ColDate = 0,
        ColTaxDeclaringCountry,
        ColTaxScheme,
        ColTaxJurisdiction,
        ColCountryVatPaidTo,
        ColChannel,
        ColStore,
        ColInvoiceNumber,
        ColCount
    };

    static const QStringList HEADER_IDS;

private:
    void _load();
    void _save();

    struct InvoiceRecord {
        QDate date;
        QString taxDeclaringCountry;
        QString taxScheme;
        QString taxJurisdiction;
        QString countryVatPaidTo;
        QString channel;
        QString store;
        QString invoiceNumber;
        QString shipmentId;  // eventId — used as orderId in PDF and for address lookup
        QString activityId;  // activityId for revision records (e.g. "3105_refund"); empty
                             // for base records. When non-empty, regenerateInvoices uses
                             // this to fetch the correct invoicingInfo and to record the
                             // generated invoice under the right key in OrderManager.
    };

    QString _buildContextKey(
        const QDate &date,
        const TaxResolver::TaxContext &taxContext,
        const QString &channel,
        const QString &store) const;

    int _getNextSequenceForContext(const QString &contextKey);

public:
    // Remove the invoice record associated with a shipmentId (base + any revisions).
    // Call this before deleting the corresponding sale so that if the sale is
    // recreated, the same invoice number is re-assigned from scratch.
    void removeInvoiceRecord(const QString &shipmentId);

    // Remove the invoice record identified by its invoice number plus any -Rxx revision
    // records.  Called from ServiceSalesBooksTable::remove() when an invoice has already
    // been generated (i.e. the number is stored in the OrderManager).
    void removeInvoiceByNumber(const QString &invoiceNumber);

private:
    

    QString m_filePath;
    QList<InvoiceRecord> m_data;
    const CompanyInfosTable *m_companyInfos;
    const CompanyAddressTable *m_companyAddress;
    const CurrencyRateManager *m_currencyRates;
    const VatNumbersTable *m_vatNumbers;

    // Cache for tracking sequences per context (YYYYMM-{scheme}-{country}-{channel}-{store})
    QHash<QString, int> m_sequenceCache;
};

#endif // INVOICEGENERATOR_H
