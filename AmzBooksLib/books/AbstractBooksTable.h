#ifndef ABSTRACTBOOKSTABLE_H
#define ABSTRACTBOOKSTABLE_H

#include <QAbstractTableModel>
#include <QDir>
#include <QDate>

class BooksConnections;
class Shipment;

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

    static const int IND_DATE = 0;
    static const int IND_AMOUNT = 1;
    static const int IND_CURRENCY = 2;
    static const int IND_LABEL = 3;
    static const int IND_ACCOUNT1 = 4;
    static const int IND_ACCOUNT2 = 5;

    explicit AbstractBooksTable(
            const BooksConnections *bookConnections,
            const QDir &workingDir,
            QObject *parent = nullptr);
    
    void init();

    QString getRowId(const QModelIndex &index) const;
    
    // Helpers
    QDate getDate(int row) const;
    double getAmount(int row) const;
    QString getCurrency(int row) const;
    QString getLabel(int row) const;
    QString getAccount1(int row) const;
    QString getAccount2(int row) const;

    static bool isGroupedOrders(const Shipment *shipment);

    virtual QString getId() const = 0;
    virtual void load(int year) = 0;

    void add(const QString &rowId,
             const QString &bookId,
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
    virtual bool remove(const QString &rowId);

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
