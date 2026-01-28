#ifndef FBACENTERSTABLE_H
#define FBACENTERSTABLE_H

#include <QAbstractTableModel>
#include <QDir>
#include <QVariant>
#include <QCoroTask>
#include <functional>

class FbaCentersTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    struct FbaCenter {
        QString centerId;
        QString countryCode;
        QString postalCode;
        QString city;
    };

    explicit FbaCentersTable(const QDir &workingDir, QObject *parent = nullptr);

    // QCoro-based retrieval with user interaction callback
    QCoro::Task<QString> getCountryCode(
        const QString &fbaCenterId,
        std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing = nullptr) const;

    void addCenter(const FbaCenter &center);

    // Header:
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Basic functionality:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // Editable:
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    static const QStringList HEADER_IDS;
    static const QStringList HEADER_NAMES; // Fallback or display names if needed, usually tr() in impl

    QList<QStringList> m_listOfStringList;
    QString m_filePath;
    
    // Cache for fast lookup: FbaCenterId -> FbaCenter struct (or just CountryCode)
    // Map CenterID -> Row Index (or struct)
    QHash<QString, FbaCenter> m_cache;

    void _fillIfEmpty();
    void _save();
    void _load();
    void _rebuildCache();
};

#endif // FBACENTERSTABLE_H
