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
    QObject::tr("Default Amount")
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

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        int row = index.row();
        int col = index.column();
        if (row >= 0 && row < m_clients.size() && col >= 0 && col < m_clients[row].size()) {
            // Convert to double for Amount column if DisplayRole? 
            // Usually CSV stores as string.
            return m_clients[row][col];
        }
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
                                     const QString &currency, double defaultAmount)
{
    beginInsertRows(QModelIndex(), m_clients.size(), m_clients.size());
    QStringList row;
    row << clientName << serviceLabel << country << vatNumber << currency << QString::number(defaultAmount);
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
double ServiceClientManager::getDefaultAmount(int row) const
{
    if (row >= 0 && row < m_clients.size()) return m_clients[row][ColDefaultAmount].toDouble();
    return 0.0;
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
