#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include "books/BookAccountBankTable.h"
#include "PaneSettingsBankAccounts.h"
#include "ui_PaneSettingsBankAccounts.h"

PaneSettingsBankAccounts::PaneSettingsBankAccounts(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PaneSettingsBankAccounts)
{
    ui->setupUi(this);

    QDir workingDir{WorkingDirectoryManager::instance()->workingDir()};

    auto *model = new BookAccountBankTable{workingDir, ui->tableBankAccounts};
    ui->tableBankAccounts->setModel(model);
}

PaneSettingsBankAccounts::~PaneSettingsBankAccounts()
{
    delete ui;
}
