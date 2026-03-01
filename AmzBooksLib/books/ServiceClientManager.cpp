#include "ServiceClientManager.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>

const QStringList ServiceClientManager::COL_NAMES = {
    QObject::tr("Client Name"),
    QObject::tr("Service Label"),
    QObject::tr("Country"),
    QObject::tr("VAT Number"),
    QObject::tr("Currency"),
    QObject::tr("Payment Type"),
    QObject::tr("Payment Days"),
    QObject::tr("Street 1"),
    QObject::tr("Street 2"),
    QObject::tr("Postal Code"),
    QObject::tr("City"),
    QObject::tr("Account Sale 7"),
    QObject::tr("Account VAT"),
    QObject::tr("Account"),
    QObject::tr("VAT on Payment")
};

ServiceClientManager::ServiceClientManager(const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
    , m_workingDir(workingDir)
{
    m_filePath = m_workingDir.absoluteFilePath("serviceClient.csv");
    _load();
}

int ServiceClientManager::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_clients.size();
}

int ServiceClientManager::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return COL_NAMES.size();
}

QVariant ServiceClientManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    int row = index.row();
    int col = index.column();

    if ((role == Qt::DisplayRole || role == Qt::EditRole)
            && row >= 0 && row < m_clients.size()
            && col >= 0 && col < m_clients[row].size()) {

        if (role == Qt::DisplayRole && col == ColPaymentType) {
            static const QStringList labels = {
                tr("Instant"), tr("After X Days"), tr("End of Next Month")
            };
            bool ok;
            int idx = m_clients[row][col].toInt(&ok);
            if (ok && idx >= 0 && idx < labels.size())
                return labels[idx];
        }

        return m_clients[row][col];
    }
    return QVariant();
}

QVariant ServiceClientManager::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole) {
        if (orientation == Qt::Horizontal) {
            if (section >= 0 && section < COL_NAMES.size())
                return COL_NAMES[section];
        } else {
            return QString::number(section + 1);
        }
    }
    return QVariant();
}

Qt::ItemFlags ServiceClientManager::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return QAbstractItemModel::flags(index) | Qt::ItemIsEditable;
}

bool ServiceClientManager::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (index.isValid() && role == Qt::EditRole) {
        int row = index.row();
        int col = index.column();
        if (row >= 0 && row < m_clients.size() && col >= 0 && col < m_clients[row].size()) {
            if (m_clients[row][col] != value.toString()) {
                m_clients[row][col] = value.toString();
                emit dataChanged(index, index, {role});
                _save();
                return true;
            }
        }
    }
    return false;
}

void ServiceClientManager::addClient(const QString &clientName, const QString &serviceLabel,
                                     const QString &country, const QString &vatNumber,
                                     const QString &currency,
                                     PaymentType paymentType, int paymentDays,
                                     const QString &street1, const QString &street2,
                                     const QString &postalCode, const QString &city,
                                     const QString &accountSale7, const QString &accountVat,
                                     const QString &account, bool vatOnPayment)
{
    beginInsertRows(QModelIndex(), m_clients.size(), m_clients.size());
    QStringList row;
    row << clientName << serviceLabel << country << vatNumber << currency
        << QString::number(static_cast<int>(paymentType))
        << QString::number(paymentDays)
        << street1 << street2 << postalCode << city
        << accountSale7 << accountVat << account
        << (vatOnPayment ? QStringLiteral("1") : QStringLiteral("0"));
    m_clients.append(row);
    endInsertRows();
    _save();
}

void ServiceClientManager::removeClient(int row)
{
    if (row >= 0 && row < m_clients.size()) {
        beginRemoveRows(QModelIndex(), row, row);
        m_clients.removeAt(row);
        endRemoveRows();
        _save();
    }
}

QString ServiceClientManager::getClientName(int row) const
{
    if (row >= 0 && row < m_clients.size()) return m_clients[row][ColClientName];
    return QString();
}
QString ServiceClientManager::getServiceLabel(int row) const
{
    if (row >= 0 && row < m_clients.size()) return m_clients[row][ColServiceLabel];
    return QString();
}
QString ServiceClientManager::getCountry(int row) const
{
    if (row >= 0 && row < m_clients.size()) return m_clients[row][ColCountry];
    return QString();
}
QString ServiceClientManager::getVatNumber(int row) const
{
    if (row >= 0 && row < m_clients.size()) return m_clients[row][ColVatNumber];
    return QString();
}
QString ServiceClientManager::getCurrency(int row) const
{
    if (row >= 0 && row < m_clients.size()) return m_clients[row][ColCurrency];
    return QString();
}
PaymentType ServiceClientManager::getPaymentType(int row) const
{
    if (row >= 0 && row < m_clients.size() && m_clients[row].size() > ColPaymentType) {
        return static_cast<PaymentType>(m_clients[row][ColPaymentType].toInt());
    }
    return PaymentType::Instant;
}

int ServiceClientManager::getPaymentDays(int row) const
{
    if (row >= 0 && row < m_clients.size() && m_clients[row].size() > ColPaymentDays) {
        return m_clients[row][ColPaymentDays].toInt();
    }
    return 0;
}

QString ServiceClientManager::getStreet1(int row) const
{
    if (row >= 0 && row < m_clients.size() && m_clients[row].size() > ColStreet1)
        return m_clients[row][ColStreet1];
    return QString();
}

QString ServiceClientManager::getStreet2(int row) const
{
    if (row >= 0 && row < m_clients.size() && m_clients[row].size() > ColStreet2)
        return m_clients[row][ColStreet2];
    return QString();
}

QString ServiceClientManager::getPostalCode(int row) const
{
    if (row >= 0 && row < m_clients.size() && m_clients[row].size() > ColPostalCode)
        return m_clients[row][ColPostalCode];
    return QString();
}

QString ServiceClientManager::getCity(int row) const
{
    if (row >= 0 && row < m_clients.size() && m_clients[row].size() > ColCity)
        return m_clients[row][ColCity];
    return QString();
}

QString ServiceClientManager::getAccountSale7(int row) const
{
    if (row >= 0 && row < m_clients.size() && m_clients[row].size() > ColAccountSale7)
        return m_clients[row][ColAccountSale7];
    return QString();
}

QString ServiceClientManager::getAccountVat(int row) const
{
    if (row >= 0 && row < m_clients.size() && m_clients[row].size() > ColAccountVat)
        return m_clients[row][ColAccountVat];
    return QString();
}

QString ServiceClientManager::getAccount(int row) const
{
    if (row >= 0 && row < m_clients.size() && m_clients[row].size() > ColAccount)
        return m_clients[row][ColAccount];
    return QString();
}

bool ServiceClientManager::getVatOnPayment(int row) const
{
    if (row >= 0 && row < m_clients.size() && m_clients[row].size() > ColVatOnPayment)
        return m_clients[row][ColVatOnPayment] == QStringLiteral("1");
    return false;
}

QDate ServiceClientManager::calculatePaymentDate(int row, const QDate &orderDate) const
{
    PaymentType type = getPaymentType(row);
    switch (type) {
    case PaymentType::Instant:
        return orderDate;
    case PaymentType::AfterXDays: {
        int days = getPaymentDays(row);
        return orderDate.addDays(days);
    }
    case PaymentType::EndOfNextMonth: {
        // Move to next month, then go to end of that month
        QDate nextMonth = orderDate.addMonths(1);
        return QDate(nextMonth.year(), nextMonth.month(), nextMonth.daysInMonth());
    }
    }
    return orderDate;
}

QString ServiceClientManager::paymentTypeLabel(PaymentType type)
{
    switch (type) {
    case PaymentType::Instant:        return tr("Instant");
    case PaymentType::AfterXDays:     return tr("After X Days");
    case PaymentType::EndOfNextMonth: return tr("End of Next Month");
    }
    return tr("Instant");
}

QStringList ServiceClientManager::paymentTypeLabels()
{
    return {
        paymentTypeLabel(PaymentType::Instant),
        paymentTypeLabel(PaymentType::AfterXDays),
        paymentTypeLabel(PaymentType::EndOfNextMonth)
    };
}

PaymentType ServiceClientManager::paymentTypeFromLabel(const QString &label)
{
    if (label == paymentTypeLabel(PaymentType::AfterXDays))     return PaymentType::AfterXDays;
    if (label == paymentTypeLabel(PaymentType::EndOfNextMonth)) return PaymentType::EndOfNextMonth;
    return PaymentType::Instant;
}

void ServiceClientManager::_load()
{
    m_clients.clear();
    QFile file(m_filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream in(&file);
    // Skip header if present? Or auto-detect? 
    // Usually we assume header exists or we read all.
    // Let's assume header exists.
    bool first = true;
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (first) {
            // Check if it matches header
            // If it looks like header, skip.
            // Simple check: first token is "Client Name" (localized?) or "ClientName"
            // Since we use tr(), the file header might depend on locale if generated by this app.
            // But usually we want stable ID in CSV.
            // For now, let's assume the first line is ALWAYS header and skip it.
            first = false;
            continue;
        }
        
        QStringList parts = line.split(";"); // Semicolon separator
        // Ensure standard size
        while (parts.size() < ColCount) parts << "";
        m_clients.append(parts);
    }
}

void ServiceClientManager::_save()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to save ServiceClientManager:" << file.errorString();
        return;
    }

    QTextStream out(&file);
    // Write Header
    // We should probably write English constants or localized?
    // Let's write localized as per COL_NAMES, consistent with load.
    out << COL_NAMES.join(";") << "\n";

    for (const QStringList &row : m_clients) {
        out << row.join(";") << "\n";
    }
}
