#include "PaneSettingsGroupedSaleAccounts.h"
#include "ui_PaneSettingsGroupedSaleAccounts.h"

#include <QHeaderView>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"
#include "books/BookAccountsGroupedSalesTable.h"
#include "orders/AbstractImporterFile.h"
#include "orders/AbstractImporterApi.h"

PaneSettingsGroupedSaleAccounts::PaneSettingsGroupedSaleAccounts(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneSettingsGroupedSaleAccounts)
{
    ui->setupUi(this);

    QDir workingDir{WorkingDirectoryManager::instance()->workingDir()};

    auto *groupedTable = new BookAccountsGroupedSalesTable{workingDir, this};

    // Populate channels from all registered importers (file + API), deduplicating by channel.
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
}

PaneSettingsGroupedSaleAccounts::~PaneSettingsGroupedSaleAccounts()
{
    delete ui;
}
