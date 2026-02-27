#include "AmzPaymentSettings.h"

#include <QFile>
#include <QTextStream>

const QString AmzPaymentSettings::FILE_NAME = QStringLiteral("amazon_payment_settings.csv");
const QString AmzPaymentSettings::ID_DEBIT   = QStringLiteral("debit_account");
const QString AmzPaymentSettings::ID_CREDIT  = QStringLiteral("credit_account");
const QString AmzPaymentSettings::ID_AMAZON_ACCOUNT = QStringLiteral("amazon_account");

AmzPaymentSettings::AmzPaymentSettings(const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
{
    m_filePath = workingDir.absoluteFilePath(FILE_NAME);
    _load();
    _ensureDefaults();
}

// ─── public getters ───────────────────────────────────────────────────────────

QString AmzPaymentSettings::getAccountDebit() const
{
    int idx = _rowIndexById(ID_DEBIT);
    return idx >= 0 ? m_rows[idx][2] : QString();
}

QString AmzPaymentSettings::getAccountCredit() const
{
    int idx = _rowIndexById(ID_CREDIT);
    return idx >= 0 ? m_rows[idx][2] : QString();
}

QString AmzPaymentSettings::getAmazonAccount() const
{
    int idx = _rowIndexById(ID_AMAZON_ACCOUNT);
    return idx >= 0 ? m_rows[idx][2] : QString();
}

// ─── QAbstractTableModel ─────────────────────────────────────────────────────

int AmzPaymentSettings::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_rows.size();
}

int AmzPaymentSettings::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return 2; // Param | Value  (ID is hidden in the CSV only)
}

QVariant AmzPaymentSettings::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return QVariant();

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        if (index.column() == 0)
            // Always use the current translation regardless of what was stored
            return _paramForId(m_rows[index.row()][0]);
        if (index.column() == 1)
            return m_rows[index.row()][2]; // Value
    }
    return QVariant();
}

bool AmzPaymentSettings::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole || index.column() != 1)
        return false;
    if (index.row() >= m_rows.size())
        return false;

    m_rows[index.row()][2] = value.toString();
    _save();
    emit dataChanged(index, index, {role});
    return true;
}

QVariant AmzPaymentSettings::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        switch (section) {
        case 0: return tr("Param");
        case 1: return tr("Value");
        }
    }
    return QVariant();
}

Qt::ItemFlags AmzPaymentSettings::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (index.column() == 1)
        f |= Qt::ItemIsEditable;
    return f;
}

// ─── private helpers ──────────────────────────────────────────────────────────

int AmzPaymentSettings::_rowIndexById(const QString &id) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i][0] == id)
            return i;
    }
    return -1;
}

QString AmzPaymentSettings::_paramForId(const QString &id) const
{
    if (id == ID_DEBIT)  return tr("Debit account");
    if (id == ID_CREDIT) return tr("Credit account");
    if (id == ID_AMAZON_ACCOUNT) return tr("Amazon Purchase Account");
    return id; // fallback for unknown future rows
}

void AmzPaymentSettings::_ensureDefaults()
{
    // Called from the constructor before any view is attached, so no signals needed.
    bool changed = false;

    if (_rowIndexById(ID_DEBIT) < 0) {
        m_rows.prepend({ID_DEBIT, _paramForId(ID_DEBIT), QString()});
        changed = true;
    }
    if (_rowIndexById(ID_CREDIT) < 0) {
        int debitIdx = _rowIndexById(ID_DEBIT);
        m_rows.insert(debitIdx + 1, {ID_CREDIT, _paramForId(ID_CREDIT), QString()});
        changed = true;
    }
    if (_rowIndexById(ID_AMAZON_ACCOUNT) < 0) {
        int creditIdx = _rowIndexById(ID_CREDIT);
        m_rows.insert(creditIdx + 1, {ID_AMAZON_ACCOUNT, _paramForId(ID_AMAZON_ACCOUNT), QStringLiteral("FAMZMK")});
        changed = true;
    }

    if (changed)
        _save();
}

void AmzPaymentSettings::_save()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream out(&file);
    out << "ID;Param;Value\n";
    for (const QStringList &row : m_rows) {
        QStringList padded = row;
        while (padded.size() < 3) padded << QString();
        out << padded.join(";") << "\n";
    }
}

void AmzPaymentSettings::_load()
{
    m_rows.clear();

    QFile file(m_filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream in(&file);
    if (in.atEnd()) return;

    // Map column positions by header name for forward-compatibility
    QStringList headers = in.readLine().split(";");
    QMap<QString, int> colMap;
    for (int i = 0; i < headers.size(); ++i)
        colMap[headers[i].trimmed()] = i;

    const int idxId    = colMap.value(QStringLiteral("ID"),    -1);
    const int idxParam = colMap.value(QStringLiteral("Param"), -1);
    const int idxValue = colMap.value(QStringLiteral("Value"), -1);

    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty()) continue;

        const QStringList parts = line.split(";");
        const QString id    = (idxId    >= 0 && idxId    < parts.size()) ? parts[idxId].trimmed()    : QString();
        const QString param = (idxParam >= 0 && idxParam < parts.size()) ? parts[idxParam].trimmed() : QString();
        const QString value = (idxValue >= 0 && idxValue < parts.size()) ? parts[idxValue].trimmed() : QString();

        if (id.isEmpty()) continue;
        m_rows.append({id, param, value});
    }
}
