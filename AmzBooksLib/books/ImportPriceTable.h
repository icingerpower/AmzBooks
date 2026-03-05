#ifndef IMPORTPRICETABLE_H
#define IMPORTPRICETABLE_H

#include <QObject>
#include <QHash>
#include <QDir>
#include <QString>

/**
 * @brief Stores per-country shipping prices (price / KG) by year.
 *
 * Data is persisted to "import-prices.csv" in the working directory.
 * A single instance of this class is shared across all WidgetPurchases in the
 * application so that editing the values in one widget immediately reflects in
 * all others via the pricesChanged signal.
 *
 * It uses year 0 as a legacy fallback for old data.
 */
class ImportPriceTable : public QObject
{
    Q_OBJECT

public:
    explicit ImportPriceTable(const QDir &workingDir, QObject *parent = nullptr);

    /** Returns the shipping price for the given year and country code.
     *  Falls back to the year 0, and then the Default country row (""). */
    double getShippingPrice(int year, const QString &countryCode) const;

    /** Convenience setter */
    void setShippingPrice(int year, const QString &countryCode, double price);

    /** Returns true if no CSV file existed when this instance was constructed
     *  (i.e. all prices were initialised to 0.0).  Callers that have access to
     *  legacy persistence (e.g. QSettings) can use this flag to migrate old
     *  values before the first save. */
    bool wasNewlyCreated() const { return m_wasNewlyCreated; }

signals:
    void pricesChanged();

private:
    void _load();
    void _save() const;

    QString m_filePath;
    // Map: Year -> (CountryCode -> Price)
    QHash<int, QHash<QString, double>> m_prices;
    bool m_wasNewlyCreated = false;
};

#endif // IMPORTPRICETABLE_H
