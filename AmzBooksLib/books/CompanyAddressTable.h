#ifndef COMPANYADDRESSTABLE_H
#define COMPANYADDRESSTABLE_H

#include <QAbstractTableModel>
#include <QDate>
#include <QPair>
#include <QList>
#include <QObject>
#include <QDir>

class CompanyAddressTable : public QAbstractTableModel
{
    Q_OBJECT

public:

    static const QStringList HEADER_IDS;
    explicit CompanyAddressTable(const QDir &workingDir, QObject *parent = nullptr);
    void remove(const QModelIndex &index);

    QString getCompanyAddress(const QDate &date) const;
    QString getCompanyName(const QDate &date) const;
    QString getStreet1(const QDate &date) const;
    QString getStreet2(const QDate &date) const;
    QString getPostalCode(const QDate &date) const;
    QString getCity(const QDate &date) const;

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool insertRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

private:
    void _load();
    void _save();
    void _sort();
    
    struct AddressItem {
        QDate date;
        QString companyName;
        QString street1;
        QString street2;
        QString postalCode;
        QString city;
    };
    
    // Helper
    const AddressItem &_getItemForDate(const QDate &date) const;

    QString m_filePath;
    QList<AddressItem> m_data; // Sorted by date descending
};

#endif // COMPANYADDRESSTABLE_H
