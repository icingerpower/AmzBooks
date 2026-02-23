#ifndef BOOKACCOUNTSELFVATTABLE_H
#define BOOKACCOUNTSELFVATTABLE_H

#include <QAbstractTableModel>
#include <QDir>

class BookAccountSelfVatTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit BookAccountSelfVatTable(const QDir &workingDir,
                                     const QString &companyCountryCode,
                                     QObject *parent = nullptr);

    QString getAccountVatDeductible(const QString &countryFrom, const QString &countryTo) const;
    QString getAccountVatDue(const QString &countryFrom, const QString &countryTo) const;

    // Header:
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Basic functionality:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // Editable:
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    static const QStringList HEADER;
    static const int ROW_EU = 0;
    static const int ROW_NON_EU = 1;
    static const int COL_COUNTRY = 0;
    static const int COL_DEDUCTIBLE = 1;
    static const int COL_DUE = 2;

    // m_rows[ROW_EU]     = { label, deductibleAccount, dueAccount }
    // m_rows[ROW_NON_EU] = { label, deductibleAccount, dueAccount }
    QList<QStringList> m_rows;

    QString m_filePath;
    QString m_companyCountryCode;

    void _fillIfEmpty();
    void _save();
    void _load();

    // Returns "EU", "NON_EU", or "" if not applicable
    QString _resolveCategory(const QString &countryFrom, const QString &countryTo) const;
};

#endif // BOOKACCOUNTSELFVATTABLE_H
