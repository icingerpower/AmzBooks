#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include "books/BookAccountAmzBalanceTable.h"
#include "PaneSettingsAmzBalanceAccounts.h"
#include "ui_PaneSettingsAmzBalanceAccounts.h"

PaneSettingsAmzBalanceAccounts::PaneSettingsAmzBalanceAccounts(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PaneSettingsAmzBalanceAccounts)
{
    ui->setupUi(this);

    QDir workingDir{WorkingDirectoryManager::instance()->workingDir()};

    auto *model = new BookAccountAmzBalanceTable{workingDir, ui->tableAmzBalanceAccounts};
    ui->tableAmzBalanceAccounts->setModel(model);
}

PaneSettingsAmzBalanceAccounts::~PaneSettingsAmzBalanceAccounts()
{
    delete ui;
}
