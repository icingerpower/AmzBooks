#ifndef PARAMSTABLE_H
#define PARAMSTABLE_H

#include <QAbstractTableModel>
#include "AbstractImporter.h"

class ParamsTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit ParamsTable(AbstractImporter *importer, QObject *parent = nullptr);

    enum Columns {
        ColParamName = 0,
        ColParamValue,
        ColCount
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    AbstractImporter *m_importer;
    QStringList m_keys;

signals:
    void exceptionOccurred(const QString &title, const QString &message);
};

#endif // PARAMSTABLE_H
