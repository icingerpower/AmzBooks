#ifndef IMPORTERTABLE_H
#define IMPORTERTABLE_H

#include <QAbstractTableModel>
#include "AbstractImporter.h"

class ImporterTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit ImporterTable(QObject *parent = nullptr);

    enum Columns {
        ColName = 0,
        ColChannel,
        ColSubchannel,
        ColReport,
        ColCount
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

protected:
    QList<const AbstractImporter*> m_importers;

    // Helper to sort importers
    void sortImporters();
};

#endif // IMPORTERTABLE_H
