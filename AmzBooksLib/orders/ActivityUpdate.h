#ifndef ACTIVITYUPDATE_H
#define ACTIVITYUPDATE_H

#include <QAbstractTableModel>
#include <QList>
#include <QDateTime>

struct ActivityUpdateItem {
    QDateTime date;
    QString type;     // "Invoice", "CreditNote", "ShipmentUpdate"
    QString number;   // ID
    double amount;
    QString currency;
    QString status;
};

class ActivityUpdate : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit ActivityUpdate(QObject *parent = nullptr);

    void setItems(const QList<ActivityUpdateItem> &items);
    
    // QAbstractTableModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    QList<ActivityUpdateItem> m_items;
};

#endif // ACTIVITYUPDATE_H
