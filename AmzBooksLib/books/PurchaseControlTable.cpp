#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

#include "PurchaseControlTable.h"

const QString PurchaseControlTable::COL_ID_SUPPLIER = QStringLiteral("supplier_account");
const QString PurchaseControlTable::COL_ID_LABEL    = QStringLiteral("label");
const QString PurchaseControlTable::COL_ID_FREQUENCY = QStringLiteral("frequency");

QStringList PurchaseControlTable::allFrequencyCodes()
{
    return {
        FREQ_ALL,
        FREQ_JAN, FREQ_FEB, FREQ_MAR,
        FREQ_APR, FREQ_MAY, FREQ_JUN,
        FREQ_JUL, FREQ_AUG, FREQ_SEP,
        FREQ_OCT, FREQ_NOV, FREQ_DEC
    };
}

QString PurchaseControlTable::frequencyDisplayText(const QString &code)
{
    // Using QCoreApplication::translate so this static method is translatable
    // even without a QObject context.
    if (code == FREQ_ALL) { return QCoreApplication::translate("PurchaseControlTable", "All months"); }
    if (code == FREQ_JAN) { return QCoreApplication::translate("PurchaseControlTable", "January"); }
    if (code == FREQ_FEB) { return QCoreApplication::translate("PurchaseControlTable", "February"); }
    if (code == FREQ_MAR) { return QCoreApplication::translate("PurchaseControlTable", "March"); }
    if (code == FREQ_APR) { return QCoreApplication::translate("PurchaseControlTable", "April"); }
    if (code == FREQ_MAY) { return QCoreApplication::translate("PurchaseControlTable", "May"); }
    if (code == FREQ_JUN) { return QCoreApplication::translate("PurchaseControlTable", "June"); }
    if (code == FREQ_JUL) { return QCoreApplication::translate("PurchaseControlTable", "July"); }
    if (code == FREQ_AUG) { return QCoreApplication::translate("PurchaseControlTable", "August"); }
    if (code == FREQ_SEP) { return QCoreApplication::translate("PurchaseControlTable", "September"); }
    if (code == FREQ_OCT) { return QCoreApplication::translate("PurchaseControlTable", "October"); }
    if (code == FREQ_NOV) { return QCoreApplication::translate("PurchaseControlTable", "November"); }
    if (code == FREQ_DEC) { return QCoreApplication::translate("PurchaseControlTable", "December"); }
    return code; // unknown code: display as-is
}

PurchaseControlTable::PurchaseControlTable(const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
    , m_workingDir(workingDir)
{
    _load();
}

int PurchaseControlTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_data.size();
}

int PurchaseControlTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return 3;
}

QVariant PurchaseControlTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_data.size()) {
        return QVariant();
    }
    const auto &entry = m_data.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case 0: return entry.supplierAccount;
        case 1: return entry.label;
        case 2: return frequencyDisplayText(entry.frequencyCode);
        default: break;
        }
    } else if (role == Qt::EditRole) {
        switch (index.column()) {
        case 0: return entry.supplierAccount;
        case 1: return entry.label;
        case 2: return entry.frequencyCode; // stable code, not display text
        default: break;
        }
    }
    return QVariant();
}

bool PurchaseControlTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole || index.row() >= m_data.size()) {
        return false;
    }
    auto &entry = m_data[index.row()];
    switch (index.column()) {
    case 0: entry.supplierAccount = value.toString(); break;
    case 1: entry.label           = value.toString(); break;
    case 2: entry.frequencyCode   = value.toString(); break;
    default: return false;
    }
    _save();
    emit dataChanged(index, index, {Qt::EditRole, Qt::DisplayRole});
    return true;
}

QVariant PurchaseControlTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        switch (section) {
        case 0: return tr("Supplier account");
        case 1: return tr("Label");
        case 2: return tr("Frequency");
        default: break;
        }
    }
    return QVariant();
}

Qt::ItemFlags PurchaseControlTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return QAbstractItemModel::flags(index) | Qt::ItemIsEditable;
}

void PurchaseControlTable::addEntry(const QString &supplierAccount,
                                    const QString &label,
                                    const QString &frequencyCode)
{
    const int row = m_data.size();
    beginInsertRows(QModelIndex(), row, row);
    m_data.append({supplierAccount, label, frequencyCode});
    endInsertRows();
    _save();
}

bool PurchaseControlTable::removeRows(int row, int count, const QModelIndex &parent)
{
    if (row < 0 || count <= 0 || row + count > m_data.size()) {
        return false;
    }
    beginRemoveRows(parent, row, row + count - 1);
    m_data.remove(row, count);
    endRemoveRows();
    _save();
    return true;
}

QString PurchaseControlTable::_csvFilePath() const
{
    return m_workingDir.filePath(QStringLiteral("purchase_control.csv"));
}

void PurchaseControlTable::_load()
{
    beginResetModel();
    m_data.clear();

    QFile file(_csvFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        endResetModel();
        return;
    }

    QTextStream in(&file);
    const QString headerLine = in.readLine();
    if (headerLine.isEmpty()) {
        endResetModel();
        return;
    }

    // Map stable column IDs to their position — survives reordering/additions.
    const QStringList cols = headerLine.split(';');
    const int idxSupplier  = cols.indexOf(COL_ID_SUPPLIER);
    const int idxLabel     = cols.indexOf(COL_ID_LABEL);
    const int idxFrequency = cols.indexOf(COL_ID_FREQUENCY);

    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty()) {
            continue;
        }
        const QStringList fields = line.split(';');

        ControlEntry entry;
        if (idxSupplier >= 0 && idxSupplier < fields.size()) {
            entry.supplierAccount = fields.at(idxSupplier);
        }
        if (idxLabel >= 0 && idxLabel < fields.size()) {
            entry.label = fields.at(idxLabel);
        }
        if (idxFrequency >= 0 && idxFrequency < fields.size()) {
            entry.frequencyCode = fields.at(idxFrequency);
        }
        if (!entry.supplierAccount.isEmpty()) {
            m_data.append(entry);
        }
    }

    endResetModel();
}

void PurchaseControlTable::_save() const
{
    QFile file(_csvFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        qWarning("PurchaseControlTable: failed to save %s",
                 qPrintable(_csvFilePath()));
        return;
    }

    QTextStream out(&file);
    // Stable, untranslated header IDs
    out << COL_ID_SUPPLIER << ';' << COL_ID_LABEL << ';' << COL_ID_FREQUENCY << '\n';
    for (const auto &entry : std::as_const(m_data)) {
        out << entry.supplierAccount << ';' << entry.label << ';' << entry.frequencyCode << '\n';
    }
}
