#ifndef SERVICECLIENTMANAGER_H
#define SERVICECLIENTMANAGER_H

#include <QAbstractTableModel>
#include <QDir>
#include <QList>
#include <QStringList>

class ServiceClientManager : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit ServiceClientManager(const QDir &workingDir, QObject *parent = nullptr);

    // Columns
    static const QStringList COL_NAMES;
    enum Column {
        ColClientName = 0,
        ColServiceLabel,
        ColCountry,
        ColVatNumber,
        ColCurrency,
        ColDefaultAmount,
        ColCount
    };

    // Basic functionality
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    // Modification
    void addClient(const QString &clientName, const QString &serviceLabel, 
                   const QString &country, const QString &vatNumber, 
                   const QString &currency, double defaultAmount);
    void removeClient(int row);

    // Accessors for ServiceSalesBooksTable
    QString getClientName(int row) const;
    QString getServiceLabel(int row) const;
    QString getCountry(int row) const;
    QString getVatNumber(int row) const;
    QString getCurrency(int row) const;
    double getDefaultAmount(int row) const;

private:
    void _load();
    void _save();

    QDir m_workingDir;
    QString m_filePath;
    QList<QStringList> m_clients; // Stores data as strings. Amount converted to string.
};

#endif // SERVICECLIENTMANAGER_H
