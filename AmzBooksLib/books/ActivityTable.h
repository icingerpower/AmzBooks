#ifndef ACTIVITYTABLE_H
#define ACTIVITYTABLE_H

#include <QAbstractTableModel>
#include <QList>
#include "Activity.h"

class ActivityTable : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit ActivityTable(QObject *parent = nullptr);

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    // Custom
    void addActivity(const Activity &activity);
    void addActivities(const QList<Activity> &activities);
    void clear();

private:
    QList<Activity> m_data;
};

#endif // ACTIVITYTABLE_H
