#include "ActivityTable.h"
#include <algorithm>

ActivityTable::ActivityTable(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int ActivityTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_data.size();
}

int ActivityTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return 19;
}

QVariant ActivityTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return QVariant();
    }

    switch (section) {
    case 0: return tr("Date");
    case 1: return tr("Untaxed");
    case 2: return tr("Taxes");
    case 3: return tr("Taxed");
    case 4: return tr("Currency");
    case 5: return tr("Event ID");
    case 6: return tr("Activity ID");
    case 7: return tr("Sub-Activity ID");
    case 8: return tr("From");
    case 9: return tr("To");
    case 10: return tr("VAT Paid To");
    case 11: return tr("Tax Source");
    case 12: return tr("Tax Declaring Country");
    case 13: return tr("Tax Scheme");
    case 14: return tr("Tax Jurisdiction");
    case 15: return tr("Sale Type");
    case 16: return tr("VAT Territory From");
    case 17: return tr("VAT Territory To");
    case 18: return tr("Taxes Computed");
    default: return QVariant();
    }
}

QVariant ActivityTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_data.size()) {
        return QVariant();
    }

    const Activity &activity = m_data.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case 0: return activity.getDateTime();
        case 1: return activity.getAmountUntaxed();
        case 2: return activity.getAmountTaxes();
        case 3: return activity.getAmountTaxed();
        case 4: return activity.getCurrency();
        case 5: return activity.getEventId();
        case 6: return activity.getActivityId();
        case 7: return activity.getSubActivityId();
        case 8: return activity.getCountryCodeFrom();
        case 9: return activity.getCountryCodeTo();
        case 10: return activity.getCountryCodeVatPaidTo();
        case 11: return taxSourceToString(activity.getTaxSource());
        case 12: return activity.getTaxDeclaringCountryCode();
        case 13: return taxSchemeToString(activity.getTaxScheme());
        case 14: return taxJurisdictionLevelToString(activity.getTaxJurisdictionLevel());
        case 15: return toString(activity.getSaleType());
        case 16: return activity.getVatTerritoryFrom();
        case 17: return activity.getVatTerritoryTo();
        case 18: return activity.getAmountTaxesComputed();
        }
    }

    return QVariant();
}

void ActivityTable::sort(int column, Qt::SortOrder order)
{
    emit layoutAboutToBeChanged();

    std::sort(m_data.begin(), m_data.end(), [column, order](const Activity &a, const Activity &b) {
        bool less = false;
        switch (column) {
        case 0: less = a.getDateTime() < b.getDateTime(); break;
        case 1: less = a.getAmountUntaxed() < b.getAmountUntaxed(); break;
        case 2: less = a.getAmountTaxes() < b.getAmountTaxes(); break;
        case 3: less = a.getAmountTaxed() < b.getAmountTaxed(); break;
        case 4: less = a.getCurrency() < b.getCurrency(); break;
        case 5: less = a.getEventId() < b.getEventId(); break;
        case 6: less = a.getActivityId() < b.getActivityId(); break;
        case 7: less = a.getSubActivityId() < b.getSubActivityId(); break;
        case 8: less = a.getCountryCodeFrom() < b.getCountryCodeFrom(); break;
        case 9: less = a.getCountryCodeTo() < b.getCountryCodeTo(); break;
        case 10: less = a.getCountryCodeVatPaidTo() < b.getCountryCodeVatPaidTo(); break;
        case 11: less = taxSourceToString(a.getTaxSource()) < taxSourceToString(b.getTaxSource()); break; // Sorting by string representation
        case 12: less = a.getTaxDeclaringCountryCode() < b.getTaxDeclaringCountryCode(); break;
        case 13: less = taxSchemeToString(a.getTaxScheme()) < taxSchemeToString(b.getTaxScheme()); break; // Sorting by string representation
        case 14: less = taxJurisdictionLevelToString(a.getTaxJurisdictionLevel()) < taxJurisdictionLevelToString(b.getTaxJurisdictionLevel()); break; // Sorting by string representation
        case 15: less = toString(a.getSaleType()) < toString(b.getSaleType()); break; // Sorting by string representation
        case 16: less = a.getVatTerritoryFrom() < b.getVatTerritoryFrom(); break;
        case 17: less = a.getVatTerritoryTo() < b.getVatTerritoryTo(); break;
        case 18: less = a.getAmountTaxesComputed() < b.getAmountTaxesComputed(); break;
        default: return false;
        }

        return (order == Qt::AscendingOrder) ? less : !less;
    });

    emit layoutChanged();
}

Qt::ItemFlags ActivityTable::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
        
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void ActivityTable::addActivity(const Activity &activity)
{
    beginInsertRows(QModelIndex(), m_data.size(), m_data.size());
    m_data.append(activity);
    endInsertRows();
}

void ActivityTable::addActivities(const QList<Activity> &activities)
{
    if (activities.isEmpty())
        return;

    beginInsertRows(QModelIndex(), m_data.size(), m_data.size() + activities.size() - 1);
    m_data.append(activities);
    endInsertRows();
}

void ActivityTable::clear()
{
    beginResetModel();
    m_data.clear();
    endResetModel();
}
