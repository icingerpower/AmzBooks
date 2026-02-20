#ifndef ORDERTABLEABSTRACT_H
#define ORDERTABLEABSTRACT_H

#include <QAbstractTableModel>
#include <QList>
#include <QSharedPointer>
#include <QDate>

class Shipment;
class Activity;

class OrderTableAbstract : public QAbstractTableModel
{
    Q_OBJECT

public:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

protected:
    struct OrderRow {
        QDate date;
        QString orderId;
        QString activityId;
        QString activityIdOrig;  // OrderCompleteTable: original activityId when this row is a refund/re-invoice
        QString channel;         // OrderCompleteTable: channel key from hash
        QString store;           // OrderCompleteTable: site/store key from hash (subchannel)
        QString site;            // OrderCompleteTable: orders.store value (marketplace site)
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

    explicit OrderTableAbstract(QObject *parent = nullptr);

    // Fills common fields from an activity; channel/store/activityIdOrig left empty.
    static OrderRow makeRow(const Activity &act, const QSharedPointer<Shipment> &ship);
};

#endif // ORDERTABLEABSTRACT_H
