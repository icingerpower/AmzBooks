#include "ImportPriceTable.h"

#include <QFile>
#include <QTextStream>
#include <QDebug>

// ── Fixed row order ──────────────────────────────────────────────────────────
// The table always contains exactly these rows in this order.
// An empty countryCode string represents the "Default" fallback row.
static const QList<QPair<QString /*code*/, QString /*label*/>> COUNTRY_ROWS = {
    {"",   "Default"},
    {"US", "US"},
    {"CA", "CA"},
    {"UK", "UK"},
    {"JP", "JP"},
};

// CSV serialisation label for the default row
static constexpr auto DEFAULT_CSV_LABEL = "Default";

const QStringList ImportPriceTable::HEADER_IDS = {"Country", "Price"};

// ── Construction ─────────────────────────────────────────────────────────────

ImportPriceTable::ImportPriceTable(const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
{
    m_filePath = workingDir.absoluteFilePath("import-prices.csv");

    m_wasNewlyCreated = !QFile::exists(m_filePath);
    _load(); // populates m_data (with 0.0 defaults when the file is absent)
}

// ── Public API ───────────────────────────────────────────────────────────────

double ImportPriceTable::getShippingPrice(const QString &countryCode) const
{
    // Exact match first
    for (const auto &item : m_data) {
        if (item.countryCode == countryCode)
            return item.price;
    }
    // Fallback to Default (first row)
    return m_data.isEmpty() ? 0.0 : m_data.first().price;
}

void ImportPriceTable::setShippingPrice(const QString &countryCode, double price)
{
    const int row = _rowForCountry(countryCode);
    if (row == -1) return;
    setData(index(row, 1), price, Qt::EditRole);
}

// ── QAbstractTableModel interface ────────────────────────────────────────────

int ImportPriceTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_data.size();
}

int ImportPriceTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return 2; // 0: Country label, 1: Price
}

QVariant ImportPriceTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return {};
    if (index.row() < 0 || index.row() >= m_data.size()) return {};

    const auto &item = m_data[index.row()];

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case 0:
            return item.countryCode.isEmpty() ? tr("Default") : item.countryCode;
        case 1:
            return item.price;
        }
    }
    return {};
}

QVariant ImportPriceTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        switch (section) {
        case 0: return tr("Country");
        case 1: return tr("Price / KG");
        }
    }
    return {};
}

bool ImportPriceTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole) return false;
    if (index.column() != 1) return false; // only the Price column is editable
    if (index.row() < 0 || index.row() >= m_data.size()) return false;

    bool ok = false;
    const double newPrice = value.toDouble(&ok);
    if (!ok) return false;

    // Skip no-op updates to avoid spurious dataChanged emissions
    if (qFuzzyCompare(m_data[index.row()].price, newPrice)) return true;

    m_data[index.row()].price = newPrice;
    _save();
    emit dataChanged(index, index, {role});
    return true;
}

Qt::ItemFlags ImportPriceTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = QAbstractItemModel::flags(index);
    if (index.column() == 1)
        f |= Qt::ItemIsEditable;
    return f;
}

// ── Private helpers ───────────────────────────────────────────────────────────

int ImportPriceTable::_rowForCountry(const QString &countryCode) const
{
    for (int i = 0; i < m_data.size(); ++i) {
        if (m_data[i].countryCode == countryCode)
            return i;
    }
    return -1;
}

void ImportPriceTable::_load()
{
    beginResetModel();

    // Step 1 — generate a row for every known country, defaulting to 0.0.
    // This guarantees that newly added countries are always present even when
    // the CSV was written by an older version of the application.
    m_data.clear();
    for (const auto &[code, label] : COUNTRY_ROWS)
        m_data.append({code, 0.0});

    // Step 2 — override defaults with the values stored in the CSV.
    // Rows in the CSV that do not match any known country are silently ignored.
    QFile file(m_filePath);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        in.readLine(); // skip header line

        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty()) continue;

            const QStringList parts = line.split(';');
            if (parts.size() < 2) continue;

            const QString csvLabel = parts[0].trimmed();
            const QString code     = (csvLabel == DEFAULT_CSV_LABEL) ? QString{} : csvLabel;
            const double  price    = parts[1].trimmed().toDouble();

            const int row = _rowForCountry(code);
            if (row != -1)
                m_data[row].price = price;
        }
    }

    endResetModel();
}

void ImportPriceTable::_save() const
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "ImportPriceTable: cannot write to" << m_filePath << "-" << file.errorString();
        return;
    }

    QTextStream out(&file);
    out << HEADER_IDS.join(';') << '\n';
    for (const auto &item : m_data) {
        // Use the human-readable "Default" label in the CSV instead of an empty string.
        const QString label = item.countryCode.isEmpty() ? DEFAULT_CSV_LABEL : item.countryCode;
        out << label << ';' << item.price << '\n';
    }
}

