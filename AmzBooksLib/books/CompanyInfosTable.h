#ifndef COMPANYINFOSTABLE_H
#define COMPANYINFOSTABLE_H

#include <QAbstractTableModel>
#include <QString>
#include <QList>

class CompanyInfosTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit CompanyInfosTable(const QString &iniFilePath, QObject *parent = nullptr);

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    struct InfoItem {
        QString id;
        QString parameter;
        QString value;
    };

    void _load();
    void _save();
    void _ensureDefaults();

    QString m_iniFilePath;
    QList<InfoItem> m_data;
};

#endif // COMPANYINFOSTABLE_H
