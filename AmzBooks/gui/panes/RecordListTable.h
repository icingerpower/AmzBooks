#ifndef RECORDLISTTABLE_H
#define RECORDLISTTABLE_H

#include <QAbstractTableModel>
#include <QVariantMap>

#include "orders/AbstractImporter.h"

// Table model wrapping ONE RecordList parameter of an AbstractImporter.
// Each row is a QVariantMap (fieldKey → QString); columns come from the
// param's FieldInfo list. Edits are persisted through
// AbstractImporter::setParamRecords; secret columns are masked in the display.
class RecordListTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit RecordListTable(AbstractImporter *importer, const QString &paramKey,
                             QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void addRow();
    void removeRow(int row);

signals:
    void exceptionOccurred(const QString &title, const QString &message);

private:
    AbstractImporter *m_importer = nullptr;
    QString m_key;
    QList<AbstractImporter::FieldInfo> m_fields; // column definitions
    QList<QVariantMap> m_rows;                   // in-memory rows (source of truth)

    // Persists m_rows via setParamRecords. On ExceptionWithTitleText, restores
    // `previous`, emits exceptionOccurred and returns false.
    bool _persist(const QList<QVariantMap> &previous);
};

#endif // RECORDLISTTABLE_H
