#ifndef AMZPAYMENTSETTINGS_H
#define AMZPAYMENTSETTINGS_H

#include <QAbstractTableModel>
#include <QDir>

class AmzPaymentSettings : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit AmzPaymentSettings(const QDir &workingDir, QObject *parent = nullptr);

    QString getAccountDebit() const;
    QString getAccountCredit() const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    static const QString FILE_NAME;
    static const QString ID_DEBIT;
    static const QString ID_CREDIT;

    QString m_filePath;
    // Each row: [id, param, value]  — id and param are never shown directly by the model
    QList<QStringList> m_rows;

    void _load();
    void _save();
    void _ensureDefaults();
    int  _rowIndexById(const QString &id) const;
    QString _paramForId(const QString &id) const;
};

#endif // AMZPAYMENTSETTINGS_H
