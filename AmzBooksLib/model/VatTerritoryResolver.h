#ifndef VATTERRITORYRESOLVER_H
#define VATTERRITORYRESOLVER_H

#include <QAbstractTableModel>
#include <QHash>
#include <QSet>

// VatTerritoryResolver = deterministic lookup component that maps an address location (countryCode + postalCode and optional city)
// to a canonical VatTerritoryId (e.g., "IT-LIVIGNO") and VatScope (in-VAT-area vs outside-VAT-scope) using match rules
// (exact/postal-prefix/region), with “longest postal-prefix wins”; it performs no network I/O and is unit-testable with fixed datasets.


class VatTerritoryResolver : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit VatTerritoryResolver(const QString &workingDir, QObject *parent = nullptr);

    QString getTerritoryId(const QString &countryCode, const QString &postalCode, const QString &city) const noexcept;
    void addTerritory(const QString &countryCode, const QString &postalCode, const QString &city, const QString &territoryId);
    // Header:
    QVariant headerData(int section
                        , Qt::Orientation orientation
                        , int role = Qt::DisplayRole) const override;

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
    // Cache: Country -> Postal -> {CityPattern, TerritoryId}
    // Simplification: We will just search linearly or use a better structure if performance needed.
    // Given the small number of ranges (few thousands), a smart cache is key.
    // Structure: Country -> Map<PostalCode, List<Pair<CityPattern, TerritoryId>>>
    // To handle ranges properly without exploding memory, we rely on the specific "addTerritory" being called with enumeration.
    // Existing logic used exact match for postal code. We keep that.
    struct TerritoryRule {
        QString cityPattern;
        QString territoryId;
    };
    QHash<QString, QHash<QString, QList<TerritoryRule>>> m_countryCode_postalCode_cachedTerritory;
    void _fillIfEmpty();
    void _save();
    void _load();
    void _rebuildCache();
    QString m_filePath;
};

#endif // VATTERRITORYRESOLVER_H
