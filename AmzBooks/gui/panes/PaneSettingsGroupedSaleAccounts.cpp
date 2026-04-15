#include "PaneSettingsGroupedSaleAccounts.h"
#include "ui_PaneSettingsGroupedSaleAccounts.h"

#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QSet>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"
#include "books/BookAccountsGroupedSalesTable.h"
#include "books/SaleControlTable.h"
#include "gui/delegates/SaleTypeDelegate.h"
#include "orders/AbstractImporterFile.h"
#include "orders/AbstractImporterApi.h"

PaneSettingsGroupedSaleAccounts::PaneSettingsGroupedSaleAccounts(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneSettingsGroupedSaleAccounts)
{
    ui->setupUi(this);

    QDir workingDir{WorkingDirectoryManager::instance()->workingDir()};

    // ── Grouped sale accounts table (top) ────────────────────────────────────
    auto *groupedTable = new BookAccountsGroupedSalesTable{workingDir, this};

    QList<BookAccountsGroupedSalesTable::ImporterChannelInfo> infos;
    for (const auto *importer : AbstractImporterFile::ALL_IMPORTERS()) {
        infos.append({importer->getId(), importer->getActivitySource().channel});
    }
    for (const auto *importer : AbstractImporterApi::ALL_IMPORTERS()) {
        infos.append({importer->getId(), importer->getActivitySource().channel});
    }
    groupedTable->populateChannels(infos);

    ui->tableAccounts->setModel(groupedTable);
    ui->tableAccounts->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    // ── Sale control table (bottom) ───────────────────────────────────────────
    auto *saleControlTable = new SaleControlTable{workingDir, this};
    ui->tableSaleControl->setModel(saleControlTable);
    ui->tableSaleControl->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableSaleControl->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableSaleControl->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableSaleControl->setItemDelegateForColumn(1, new SaleTypeDelegate{ui->tableSaleControl});

    connect(ui->buttonAddSaleControl, &QPushButton::clicked,
            this, &PaneSettingsGroupedSaleAccounts::addSaleControlEntry);
    connect(ui->buttonRemoveSaleControl, &QPushButton::clicked,
            this, &PaneSettingsGroupedSaleAccounts::removeSaleControlEntry);
}

PaneSettingsGroupedSaleAccounts::~PaneSettingsGroupedSaleAccounts()
{
    delete ui;
}

void PaneSettingsGroupedSaleAccounts::addSaleControlEntry()
{
    auto *model = qobject_cast<SaleControlTable *>(ui->tableSaleControl->model());
    if (!model) {
        return;
    }
    bool ok = false;
    const QString storeName = QInputDialog::getText(
        this,
        tr("Add Store"),
        tr("Store name:"),
        QLineEdit::Normal,
        QString{},
        &ok);
    if (!ok || storeName.trimmed().isEmpty()) {
        return;
    }
    model->addEntry(storeName.trimmed(), SaleControlTable::SALE_TYPE_BOTH);
}

void PaneSettingsGroupedSaleAccounts::removeSaleControlEntry()
{
    const auto &selIndexes = ui->tableSaleControl->selectionModel()->selectedIndexes();
    if (selIndexes.isEmpty()) {
        QMessageBox::warning(this,
                             tr("Selection Required"),
                             tr("Please select an entry to remove."));
        return;
    }

    if (QMessageBox::question(
                this,
                tr("Confirm Removal"),
                tr("Are you sure you want to remove the selected entry?"),
                QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    QSet<int> rows;
    for (const QModelIndex &index : selIndexes) {
        rows.insert(index.row());
    }

    QList<int> sortedRows = rows.values();
    std::sort(sortedRows.begin(), sortedRows.end(), std::greater<int>());

    auto *model = qobject_cast<SaleControlTable *>(ui->tableSaleControl->model());
    if (model) {
        for (int row : sortedRows) {
            model->removeRow(row);
        }
    }
}
