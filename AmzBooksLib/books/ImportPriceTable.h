#ifndef IMPORTPRICETABLE_H
#define IMPORTPRICETABLE_H

#include <QAbstractTableModel>
#include <QDir>
#include <QList>
#include <QString>

/**
 * @brief Stores per-country shipping prices (price / KG) as a table model.
 *
 * Data is persisted to "import-prices.csv" in the working directory.
 * A single instance of this class is shared across all WidgetPurchases in the
 * application so that editing the values in one widget immediately reflects in
 * all others via the standard QAbstractItemModel dataChanged signal.
 *
 * Rows (fixed, not editable):  Default, US, CA, UK, JP
 * Columns:                      Country (read-only) | Price / KG (editable)
 */
class ImportPriceTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    static const QStringList HEADER_IDS;

    explicit ImportPriceTable(const QDir &workingDir, QObject *parent = nullptr);

    /** Returns the shipping price for the given country code.
     *  Falls back to the Default row when the country is not found. */
    double getShippingPrice(const QString &countryCode) const;

    /** Convenience setter — equivalent to calling setData() on the price column. */
    void setShippingPrice(const QString &countryCode, double price);

    /** Returns true if no CSV file existed when this instance was constructed
     *  (i.e. all prices were initialised to 0.0).  Callers that have access to
     *  legacy persistence (e.g. QSettings) can use this flag to migrate old
     *  values before the first save. */
    bool wasNewlyCreated() const { return m_wasNewlyCreated; }

    // QAbstractTableModel interface
    int      rowCount   (const QModelIndex &parent = QModelIndex()) const override;
    int      columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data       (const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData (int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool     setData    (const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags (const QModelIndex &index) const override;

private:
    struct PriceItem {
        QString countryCode; // empty string = Default
        double  price = 0.0;
    };

    int  _rowForCountry(const QString &countryCode) const;
    void _load();
    void _save() const;

    QString          m_filePath;
    QList<PriceItem> m_data;
    bool             m_wasNewlyCreated = false;
};

#endif // IMPORTPRICETABLE_H
