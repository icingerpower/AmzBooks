#include "DialogValidOrders.h"
#include "ui_DialogValidOrders.h"
#include "gui/delegates/ComboBoxDelegate.h"

#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QHeaderView>
#include <QLocale>

// Action labels in the combo box
static const QString ACTION_FIX    = QObject::tr("Fix");
static const QString ACTION_REMOVE = QObject::tr("Remove");

// Derive a short VAT-regime label from a Taxually tax code
// (e.g. "EU|SA|20.00|G|DE|FR" → "OSS", "DE|SL|19.00|G|DE|DE" → "DOM").
// Matches the short-scheme labels used elsewhere in the app.
static QString vatRegimeLabel(const QString &taxCode)
{
    const QString first = taxCode.section(QLatin1Char('|'), 0, 0).toUpper();
    if (first == QLatin1String("EU"))   { return QStringLiteral("OSS"); }
    if (first == QLatin1String("IOSS")) { return QStringLiteral("IOSS"); }
    if (first == QLatin1String("UK"))   { return QStringLiteral("UK VOEC"); }
    if (first == QLatin1String("CH"))   { return QStringLiteral("CH VOEC"); }
    if (first.isEmpty())               { return QStringLiteral("?"); }
    return QStringLiteral("DOM");
}

DialogValidOrders::DialogValidOrders(const QList<VatOrderEntry> &entries,
                                     QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogValidOrders)
    , m_model(nullptr)
    , m_proxy(nullptr)
{
    ui->setupUi(this);
    resize(1400, 700);

    m_model = new QStandardItemModel(0, COL_COUNT, this);
    m_model->setHorizontalHeaderLabels({
        tr("Order ID"),
        tr("Date"),
        tr("Marketplace"),
        tr("SKU"),
        tr("Description"),
        tr("Countries"),
        tr("Transaction type"),
        tr("VAT regime"),
        tr("Tax code"),
        tr("Before (Amazon)"),
        tr("After (Taxually)"),
        tr("Diff"),
        tr("Action")
    });

    QLocale locale;
    for (const VatOrderEntry &entry : entries) {
        const double diff = entry.difference();
        const QString countries = QString("%1 → %2").arg(entry.departureCountry,
                                                         entry.arrivalCountry);

        auto makeItem = [](const QString &text) {
            auto *item = new QStandardItem(text);
            item->setData(text, Qt::UserRole);
            // ItemIsEditable enables double-click to open an editor so the
            // user can select and copy the cell text.
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
            return item;
        };
        auto makeNumItem = [](double value) {
            auto *item = new QStandardItem(QString::number(value, 'f', 2));
            item->setData(value, Qt::UserRole);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            return item;
        };

        // Checkable first column — also stores sourceFile and taxRate so
        // getApprovedEntries() can recover them without a visible column.
        auto *chkItem = new QStandardItem(entry.orderId);
        chkItem->setData(entry.orderId,    Qt::UserRole);
        chkItem->setData(entry.sourceFile, Qt::UserRole + 1);
        chkItem->setData(entry.taxRate,    Qt::UserRole + 2);
        chkItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
        chkItem->setCheckState(Qt::Checked);

        // Action column — stores string, edited via combo delegate
        const QString actionStr = (entry.action == VatOrderEntry::Action::Fix)
                                  ? ACTION_FIX : ACTION_REMOVE;
        auto *actItem = new QStandardItem(actionStr);
        actItem->setData(actionStr, Qt::UserRole);
        actItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);

        QList<QStandardItem *> row = {
            chkItem,
            makeItem(entry.date.toString(Qt::ISODate)),
            makeItem(entry.marketplace),
            makeItem(entry.sku),
            makeItem(entry.description),
            makeItem(countries),
            makeItem(entry.transactionType),
            makeItem(vatRegimeLabel(entry.taxCode)),
            makeItem(entry.taxCode),
            makeNumItem(entry.amazonVat),
            makeNumItem(entry.taxuallyVat),
            makeNumItem(diff),
            actItem
        };

        // Colour-code the diff column: red if taxually > amazon, blue if less
        const int sign = (diff > 0.0) ? 1 : ((diff < 0.0) ? -1 : 0);
        if (sign != 0) {
            row[COL_DIFF]->setForeground(sign > 0 ? Qt::red : Qt::blue);
        }

        m_model->appendRow(row);
    }

    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);

    ui->tableView->setModel(m_proxy);
    ui->tableView->setSortingEnabled(true);
    ui->tableView->horizontalHeader()->setStretchLastSection(false);
    ui->tableView->horizontalHeader()->setSectionResizeMode(COL_DESCRIPTION, QHeaderView::Stretch);
    ui->tableView->resizeColumnsToContents();

    // Action column: combo box delegate
    const QStringList actionOptions = {ACTION_FIX, ACTION_REMOVE};
    ui->tableView->setItemDelegateForColumn(
        COL_ACTION, new ComboBoxDelegate(actionOptions, this));

    ui->labelCount->setText(tr("%1 discrepan­cies").arg(entries.size()));

    connect(ui->checkBoxSelectAll, &QCheckBox::checkStateChanged,
            this, &DialogValidOrders::onSelectAllChanged);
}

DialogValidOrders::~DialogValidOrders()
{
    delete ui;
}

void DialogValidOrders::onSelectAllChanged(int state)
{
    const Qt::CheckState cs = static_cast<Qt::CheckState>(state);
    for (int row = 0; row < m_model->rowCount(); ++row) {
        m_model->item(row, COL_ORDER_ID)->setCheckState(cs);
    }
}

QList<VatOrderEntry> DialogValidOrders::getApprovedEntries() const
{
    // The proxy might be sorted — iterate the source model directly.
    QList<VatOrderEntry> result;
    for (int row = 0; row < m_model->rowCount(); ++row) {
        if (m_model->item(row, COL_ORDER_ID)->checkState() != Qt::Checked) {
            continue;
        }
        const QString actionStr = m_model->item(row, COL_ACTION)->text();

        const QStandardItem *idItem = m_model->item(row, COL_ORDER_ID);
        VatOrderEntry entry;
        entry.orderId        = idItem->text();
        entry.sourceFile     = idItem->data(Qt::UserRole + 1).toString();
        entry.taxRate        = idItem->data(Qt::UserRole + 2).toDouble();
        entry.date           = QDate::fromString(
                                   m_model->item(row, COL_DATE)->text(), Qt::ISODate);
        entry.marketplace    = m_model->item(row, COL_MARKETPLACE)->text();
        entry.sku            = m_model->item(row, COL_SKU)->text();
        entry.description    = m_model->item(row, COL_DESCRIPTION)->text();
        entry.transactionType = m_model->item(row, COL_TX_TYPE)->text();
        entry.taxCode        = m_model->item(row, COL_TAX_CODE)->text();
        entry.amazonVat      = m_model->item(row, COL_BEFORE)->data(Qt::UserRole).toDouble();
        entry.taxuallyVat    = m_model->item(row, COL_AFTER)->data(Qt::UserRole).toDouble();
        entry.action         = (actionStr == ACTION_REMOVE)
                               ? VatOrderEntry::Action::Remove
                               : VatOrderEntry::Action::Fix;
        result.append(entry);
    }
    return result;
}
