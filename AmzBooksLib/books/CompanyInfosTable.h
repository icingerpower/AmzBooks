#ifndef COMPANYINFOSTABLE_H
#define COMPANYINFOSTABLE_H

#include <QAbstractTableModel>
#include <QColor>
#include <QString>
#include <QList>
#include <QDir>

class CompanyInfosTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    static const QStringList HEADER_IDS;
    static const QString ID_COUNTRY;
    static const QString ID_CURRENCY;
    static const QString ID_FIXER_API_KEY;
    static const QString ID_LEGAL_SHARE_CAPITAL;
    static const QString ID_LEGAL_SIRET;
    static const QString ID_LEGAL_RCS;
    static const QString ID_LEGAL_VAT_INTRACOMMUNITY;
    static const QString ID_LEGAL_INVOICE_BOTTOM;
    static const QString ID_VAT_ON_PAYMENT_TEXT;
    static const QString ID_INTERNAL_BANK_ACCOUNT;
    explicit CompanyInfosTable(const QDir &workingDir, QObject *parent = nullptr);
    const QString &getCompanyCountryCode() const;
    static QColor getHighlightColorDark();
    const QString &getCurrency() const;
    const QString &getApiKeyFixer() const;
    const QString &getLegalID() const;
    const QString &getLegalRCS() const;
    const QString &getLegalVatIntracommunity() const;
    const QString &getInvoiceLegalBottom() const;
    const QString &getVatOnPaymentText() const;
    const QString &getInternalBankAccount() const;
    bool hadData() const;
    int getRowById(const QString &id) const;

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    struct InfoItem {
        QString id;
        QString parameter;
        QString value;
        bool encrypt = false;
    };

    void _load();
    void _save();
    void _ensureDefaults();
    void _initDefaultRows(); // New helper
    QString _encrypt(const QString &value) const;
    QString _decrypt(const QString &value) const;

    QString m_filePath;
    QList<InfoItem> m_data;
    bool m_hadData;
};

#endif // COMPANYINFOSTABLE_H
