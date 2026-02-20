#ifndef ORDERTABLE_H
#define ORDERTABLE_H

#include <QAbstractTableModel>
#include <QList>
#include <QSharedPointer>
#include <QDate>
#include "orders/OrderManager.h"
#include "books/TaxResolver.h"

class Shipment;

class OrderTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Columns {
        COL_DATE = 0,
        COL_ORDER_ID,
        COL_ACTIVITY_ID,
        COL_SALE_TYPE,
        COL_COUNTRY_FROM,
        COL_COUNTRY_TO,
        COL_VAT_PAID_TO,
        COL_IS_BUSINESS,
        COL_TAX_SOURCE,
        COL_TAX_SCHEME,
        COL_TAX_JURISDICTION,
        COL_CURRENCY,
        COL_AMOUNT_TAXED,
        COL_VAT_AMOUNT,
        COL_INVOICE_ID,
        COL_COUNT
    };

    explicit OrderTable(const QList<QSharedPointer<Shipment>> &shipments, QObject *parent = nullptr);
    explicit OrderTable(const QSharedPointer<QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext, OrderManager::ShipmentRefundsWithUpdates>>>>
                        &channel_site_ShipmentAndRefunds, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    struct OrderRow {
        QDate date;
        QString orderId;
        QString activityId;
        QString saleType;
        QString countryFrom;
        QString countryTo;
        QString vatPaidTo;
        bool isCompany = false;
        QString taxSource;
        QString taxScheme;
        QString taxJurisdiction;
        QString currency;
        double amountTaxed = 0.0;
        double vatAmount = 0.0;
        QString invoiceId;
        
        QSharedPointer<Shipment> sourceShipment;
    };

    QList<OrderRow> m_rows;
    static const QStringList COL_NAMES;

    void buildRows(const QList<QSharedPointer<Shipment>> &shipments);
    void addRows(const QList<QSharedPointer<Shipment>> &shipments);
    void buildRows(const QSharedPointer<QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext, OrderManager::ShipmentRefundsWithUpdates>>>> &data);
};

#endif // ORDERTABLE_H
