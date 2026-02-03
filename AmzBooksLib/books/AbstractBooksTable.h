#ifndef ABSTRACTBOOKSTABLE_H
#define ABSTRACTBOOKSTABLE_H

#include <QAbstractTableModel>
#include <QDir>
#include <QDate>

class BooksConnections;

class AbstractBooksTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    static const QVariantList COL_NAMES;
    static const QString KEY_AMOUNT_FULL_ORIG;
    static const QString KEY_AMOUNT_FULL_CONVERTED;
    static const QString KEY_CURRENCY_AMOUNT;
    static const QString KEY_VAT_CONVERTED;
    static const QString KEY_VAT_ORIG;
    static const QString KEY_VAT_COUNTRY;
    static const QString KEY_VAT_CURRENCY;

    explicit AbstractBooksTable(
            const BooksConnections *bookConnections,
            const QDir &workingDir,
            QObject *parent = nullptr);
    
    void init();

    QString getRowId(const QModelIndex &index) const;

    virtual QString getId() const = 0;

    void add(const QString &rowId,
             const QDate &date,
             double amountFullOrig,
             const QString &currencyAmount,
             const QString &label,
             const QString &account1,
             const QString &account2,
             double vatOrig,
             const QString &vatCountry,
             const QString &vatCurrency);
    virtual void updateData(
            const QString &rowId,
            double amountFullOrig,
            double amountFullConverted,
            const QString &currencyAmount,
            double vatConverted,
            double vatOrig,
            const QString &vatCountry,
            const QString &vatCurrency);

    void clear();
    bool remove(const QString &rowId);

    // Header:
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

private:
    void _saveInSettings();
    void _loadFromSettings();
    QList<QVariantList> m_listOfVariantList;
    QHash<QString, QHash<QString, QVariant>> m_fixedData;
    QString m_settingsFilePath;
    const BooksConnections *m_bookConnections;
};

#endif // ABSTRACTBOOKSTABLE_H
