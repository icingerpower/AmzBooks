#ifndef DIALOGVALIDORDERS_H
#define DIALOGVALIDORDERS_H

#include <QDialog>
#include <QList>

#include "vatfixer/AbstractVatFixer.h"

class QStandardItemModel;
class QSortFilterProxyModel;
class VatSummaryModel;

namespace Ui {
class DialogValidOrders;
}

// Displays a table of per-order VAT discrepancies (taxually vs Amazon).
// Each row is checked by default; unchecked rows are excluded from the result.
// The user can choose Fix (update the VAT fields) or Remove (delete the order)
// per row via a combo-box in the Action column.
class DialogValidOrders : public QDialog
{
    Q_OBJECT

public:
    explicit DialogValidOrders(const QList<VatOrderEntry> &entries,
                               QWidget *parent = nullptr);
    ~DialogValidOrders();

    // Returns entries the user has approved (checked rows), with the selected action.
    QList<VatOrderEntry> getApprovedEntries() const;

private slots:
    void onSelectAllChanged(int state);

private:
    Ui::DialogValidOrders *ui;
    QStandardItemModel    *m_model;
    QSortFilterProxyModel *m_proxy;
    VatSummaryModel       *m_summaryModel;

    // Column indices in m_model
    enum Columns {
        COL_ORDER_ID = 0,
        COL_DATE,
        COL_MARKETPLACE,
        COL_SKU,
        COL_DESCRIPTION,
        COL_COUNTRIES,     // "DE → FR"
        COL_TX_TYPE,
        COL_VAT_REGIME,    // "DOM", "OSS", "IOSS", …
        COL_TAX_CODE,
        COL_BEFORE,        // Amazon VAT
        COL_AFTER,         // Taxually VAT
        COL_DIFF,
        COL_ACTION,
        COL_COUNT
    };
};

#endif // DIALOGVALIDORDERS_H
