#ifndef VATSUMMARYMODEL_H
#define VATSUMMARYMODEL_H

#include <QAbstractTableModel>
#include <QList>

struct VatOrderEntry;

// Read-only summary model: one row per VAT country + a grand-total row.
// Cells carry ItemIsEditable so the user can open an inline editor and copy
// text without the model committing any change (setData always returns false).
class VatSummaryModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        COL_COUNTRY = 0,
        COL_NET,
        COL_TAXUALLY_VAT,
        COL_AMAZON_VAT,
        COL_DIFF,
        COL_COUNT
    };

    explicit VatSummaryModel(const QList<VatOrderEntry> &entries,
                             QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

private:
    struct Row {
        QString country;
        double  net         = 0.0;
        double  taxuallyVat = 0.0;
        double  amazonVat   = 0.0;
        bool    isTotal     = false;
    };
    QList<Row> m_rows; // country rows sorted alphabetically, grand-total last
};

#endif // VATSUMMARYMODEL_H
