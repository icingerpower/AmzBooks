#ifndef VATRESOLVER_H
#define VATRESOLVER_H

#include <QAbstractTableModel>
#include <QDate>
#include <QVariant>

#include "SaleType.h"

// Contains all vat rates, sorted by country code, and then by date (most recent rate first), and then by sale type / special product type

class VatResolver : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit VatResolver(QObject *parent = nullptr, const QString &workingDir = QString(), bool usePersistence = true);

    bool hasRate(
            const QDate &date
            , const QString &countryCode
            , SaleType saleType
            , const QString &specialProducttype = QString{}
            , const QString &vatTerritory = QString{}) const;
    double getRate(
            const QDate &date
            , const QString &countryCode
            , SaleType saleType
            , const QString &specialProducttype = QString{}
            , const QString &vatTerritory = QString{}) const;
    void recordRate(const QDate &dateFrom
                    , const QDate &dateTo
                    , SaleType saleType
                    , const QString &countryCode
                    , double vatRate);
    void recordRate(const QDate &dateFrom
                    , const QDate &dateTo
                    , SaleType saleType
                    , const QString &countryCode
                    , const QString &specialProducttype
                    , const QString &vatTerritory
                    , double vatRate);
    void remove(const QModelIndex &index);

    // Header:
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Basic functionality:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override; // Display in percent instead of rate
    bool setData(const QModelIndex &index, const QVariant &value, // Edit in percent instead of rate
                 int role = Qt::EditRole) override;

    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    static const QStringList HEADER;
    QList<QStringList> m_listOfStringList;
    QHash<QString, QHash<QString, QHash<QString, QHash<QString, QMap<QDate, double>>>>> m_countryCode_saleType_specialProductType_vatTerritory_dateFrom_vatRateCached;
    void _fillIfEmpty();
    void _save();
    void _load();
    void _rebuildCache();
    QString m_filePath;
    bool m_usePersistence;
};

#endif // VATRESOLVER_H
