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
    explicit AbstractBooksTable(
            const BooksConnections *bookConnections,
            const QDir &workingDir,
            QObject *parent = nullptr);

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
