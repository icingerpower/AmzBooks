#include "SkuRegradedTable.h"

#include <QFile>
#include <QTextStream>

static const QString CSV_FILENAME = QStringLiteral("regraded_skus.csv");
static const QStringList HEADERS  = { QStringLiteral("SKU regraded"), QStringLiteral("SKU") };

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SkuRegradedTable::SkuRegradedTable(const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
    , m_filePath(workingDir.absoluteFilePath(CSV_FILENAME))
{
    _load();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool SkuRegradedTable::appendRegradedSku(const QString &regradedSku)
{
    if (_findRow(regradedSku) != -1)
        return false;

    const int insertAt = m_rows.size();
    beginInsertRows(QModelIndex(), insertAt, insertAt);
    m_rows.append({ regradedSku, QString() });
    endInsertRows();

    _save();
    return true;
}

bool SkuRegradedTable::contains(const QString &regradedSku) const
{
    return _findRow(regradedSku) != -1;
}

QString SkuRegradedTable::getSku(const QString &regradedSku) const
{
    const int row = _findRow(regradedSku);
    if (row == -1)
        return QString();
    return m_rows.at(row).at(COL_SKU);
}

// ---------------------------------------------------------------------------
// QAbstractTableModel interface
// ---------------------------------------------------------------------------

int SkuRegradedTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_rows.size();
}

int SkuRegradedTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return COL_COUNT;
}

QVariant SkuRegradedTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();
    if (index.row() < 0 || index.row() >= m_rows.size()) return QVariant();
    if (index.column() < 0 || index.column() >= COL_COUNT) return QVariant();

    if (role == Qt::DisplayRole || role == Qt::EditRole)
        return m_rows.at(index.row()).at(index.column());

    return QVariant();
}

bool SkuRegradedTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole)
        return false;
    if (index.row() < 0 || index.row() >= m_rows.size())
        return false;
    // Only column 1 (canonical SKU) is editable; column 0 is the immutable key.
    if (index.column() != COL_SKU)
        return false;

    const QString newValue = value.toString();
    if (m_rows.at(index.row()).at(COL_SKU) == newValue)
        return false;

    m_rows[index.row()][COL_SKU] = newValue;
    emit dataChanged(index, index, { role });
    _save();
    return true;
}

QVariant SkuRegradedTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return QVariant();
    if (section >= 0 && section < HEADERS.size())
        return HEADERS.at(section);
    return QVariant();
}

Qt::ItemFlags SkuRegradedTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = QAbstractItemModel::flags(index);
    if (index.column() == COL_SKU)
        f |= Qt::ItemIsEditable;
    return f;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

int SkuRegradedTable::_findRow(const QString &regradedSku) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).at(COL_SKU_REGRADED) == regradedSku)
            return i;
    }
    return -1;
}

void SkuRegradedTable::_save()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream out(&file);
    out << HEADERS.join(QLatin1Char(';')) << "\n";
    for (const QStringList &row : m_rows)
        out << row.join(QLatin1Char(';')) << "\n";
}

void SkuRegradedTable::_load()
{
    m_rows.clear();

    QFile file(m_filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream in(&file);
    if (!in.atEnd())
        in.readLine(); // skip header

    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        QStringList parts = line.split(QLatin1Char(';'));
        while (parts.size() < COL_COUNT)
            parts << QString();

        m_rows.append(parts);
    }
}
