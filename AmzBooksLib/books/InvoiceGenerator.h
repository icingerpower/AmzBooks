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
        QObject *parent = nullptr);

    // Invoice number generation
    QString getBaseInvoiceNumber(
        const QDate &date,
        const TaxResolver::TaxContext &taxContext,
        const QString &channel,
        const QString &store);

    QStringList getNextInvoiceNumbers(
        const QDate &date,
        const TaxResolver::TaxContext &taxContext,
        const QString &channel,
        const QString &store,
        const QList<bool> &invoicesToDo,
        const std::optional<QString> &existingInvoiceNumber);

    void generateInvoice(
        const QString &invoiceNumber,
        const QString &previousInvoiceNumber,
        const QString &destinationPath,
        const Address &addressTo,
        const InvoicingInfo &invoicingInfo,
        const QString &orderId,
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
    };

    QString _buildContextKey(
        const QDate &date,
        const TaxResolver::TaxContext &taxContext,
        const QString &channel,
        const QString &store) const;

    int _getNextSequenceForContext(const QString &contextKey);
    

    QString m_filePath;
    QList<InvoiceRecord> m_data;
    const CompanyInfosTable *m_companyInfos;
    const CompanyAddressTable *m_companyAddress;
    const CurrencyRateManager *m_currencyRates;

    // Cache for tracking sequences per context (YYYYMM-{scheme}-{country}-{channel}-{store})
    QHash<QString, int> m_sequenceCache;
};

#endif // INVOICEGENERATOR_H
