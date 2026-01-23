#ifndef PURCHASEBOOKACCOUNTSTABLE_H
#define PURCHASEBOOKACCOUNTSTABLE_H

#include <QAbstractTableModel>
#include <QDir>
#include <QHash>
#include <QSet>

class PurchaseBookAccountsTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit PurchaseBookAccountsTable(const QDir &workingDir, const QString &countryCodeCompany, QObject *parent = nullptr);
    
    QString getAccountsDebit6(const QString &countryCode) const;
    QString getAccountsCredit4(const QString &countryCode) const;

    bool isDoubleVatEntryNeeded(const QString &countryFrom, const QString &countryTo) const;

    void addAccount(const QString &countryCode
                    , double vatRate
                    , const QString &vatAccountDebit6
                    , const QString &vatAccountCredit4);
    
    // Header:
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Basic functionality:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // Editable:
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    static const QStringList HEADER;
    QList<QStringList> m_listOfStringList;
    
    struct AccountPair {
        QString debit6;
        QString credit4;
    };
    QHash<QString, AccountPair> m_cache;
    QSet<QString> m_existenceCache; // Key: "Country|Rate"

    void _fillIfEmpty();
    void _save();
    void _load();
    void _rebuildCache();
    QString m_filePath;
    QString m_countryCodeCompany;
};

#endif // PURCHASEBOOKACCOUNTSTABLE_H
