#ifndef TAXAMOUNTTABLE_H
#define TAXAMOUNTTABLE_H

#include <QAbstractTableModel>
#include <QList>
#include <QSharedPointer>
#include <QDate>
#include "orders/OrderManager.h"
#include "books/TaxResolver.h"

class Shipment;
class CurrencyRateManager;

class TaxAmountTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Columns {
        COL_TAX_DECLARING_COUNTRY = 0,
        COL_TAX_SCHEME,
        COL_TAX_JURISDICTION,
        COL_VAT_PAID_TO,
        COL_AMOUNT_UNTAXED,
        COL_AMOUNT_TAXES,
        COL_AMOUNT_TOTAL,
        COL_COUNT
    };

    explicit TaxAmountTable(const QList<QSharedPointer<Shipment>> &shipments, const CurrencyRateManager *currencyRateManager, const QString &destCurrency, const QString &companyCountryCode, QObject *parent = nullptr);
    explicit TaxAmountTable(const QSharedPointer<QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext, OrderManager::ShipmentRefundsWithUpdates>>>> &data, const CurrencyRateManager *currencyRateManager, const QString &destCurrency, const QString &companyCountryCode, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
    int getNumberTotalRows() const;

private:
    struct TaxRow {
        QString taxDeclaringCountry;
        QString taxScheme;
        QString taxJurisdiction;
        QString vatPaidTo;

        double amountUntaxed = 0.0;
        double amountTaxes = 0.0;
        double amountTotal = 0.0;

        bool isTotalRow = false;
        TaxScheme taxSchemeEnum = TaxScheme::Unknown;
    };

    QList<TaxRow> m_rows;
    static const QStringList COL_NAMES;
    const CurrencyRateManager *m_currencyRateManager;
    QString m_destCurrency;
    QString m_companyCountryCode;

    void buildRows(const QList<QSharedPointer<Shipment>> &shipments);
    void buildRows(const QSharedPointer<QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext, OrderManager::ShipmentRefundsWithUpdates>>>> &data);
    void aggregate(const TaxResolver::TaxContext &ctx, const Shipment *shipment);
    void applyDefaultSort();
    void prependTotalRows();

    // Helper to store aggregation map before flattening to list
    QHash<TaxResolver::TaxContext, TaxRow> m_aggregationMap;
};

#endif // TAXAMOUNTTABLE_H
