#include "VatSummaryModel.h"
#include "AbstractVatFixer.h"

#include <QFont>
#include <QHash>
#include <algorithm>

VatSummaryModel::VatSummaryModel(const QList<VatOrderEntry> &entries, QObject *parent)
    : QAbstractTableModel(parent)
{
    QHash<QString, Row> byCountry;
    for (const VatOrderEntry &e : entries) {
        const QString key = e.arrivalCountry.isEmpty()
                            ? QStringLiteral("?") : e.arrivalCountry;
        auto &row = byCountry[key];
        row.country      = key;
        row.net         += e.netAmount;
        row.taxuallyVat += e.taxuallyVat;
        row.amazonVat   += e.amazonVat;
    }

    m_rows = byCountry.values();
    std::sort(m_rows.begin(), m_rows.end(), [](const Row &a, const Row &b) {
        return a.country < b.country;
    });

    Row total;
    total.country = tr("TOTAL");
    total.isTotal = true;
    for (const Row &r : std::as_const(m_rows)) {
        total.net         += r.net;
        total.taxuallyVat += r.taxuallyVat;
        total.amazonVat   += r.amazonVat;
    }
    m_rows.append(total);
}

int VatSummaryModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_rows.size();
}

int VatSummaryModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return COL_COUNT;
}

QVariant VatSummaryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const Row &row = m_rows.at(index.row());
    const int col  = index.column();

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (col) {
        case COL_COUNTRY:      return row.country;
        case COL_NET:          return QString::number(row.net, 'f', 2);
        case COL_TAXUALLY_VAT: return QString::number(row.taxuallyVat, 'f', 2);
        case COL_AMAZON_VAT:   return QString::number(row.amazonVat, 'f', 2);
        case COL_DIFF:         return QString::number(row.taxuallyVat - row.amazonVat, 'f', 2);
        default: break;
        }
    }
    if (role == Qt::TextAlignmentRole && col != COL_COUNTRY) {
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }
    if (role == Qt::FontRole && row.isTotal) {
        QFont f;
        f.setBold(true);
        return f;
    }
    return {};
}

QVariant VatSummaryModel::headerData(int section, Qt::Orientation orientation,
                                     int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case COL_COUNTRY:      return tr("Country");
    case COL_NET:          return tr("Net Amount");
    case COL_TAXUALLY_VAT: return tr("Taxually VAT");
    case COL_AMAZON_VAT:   return tr("Amazon VAT");
    case COL_DIFF:         return tr("Difference");
    default: break;
    }
    return {};
}

Qt::ItemFlags VatSummaryModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool VatSummaryModel::setData(const QModelIndex &, const QVariant &, int)
{
    return false;
}
