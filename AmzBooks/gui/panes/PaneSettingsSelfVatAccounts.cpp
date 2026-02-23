#include "PaneSettingsSelfVatAccounts.h"
#include "ui_PaneSettingsSelfVatAccounts.h"

#include <QHeaderView>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"
#include "books/BookAccountSelfVatTable.h"
#include "books/CompanyInfosTable.h"

PaneSettingsSelfVatAccounts::PaneSettingsSelfVatAccounts(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneSettingsSelfVatAccounts)
{
    ui->setupUi(this);

    QDir workingDir{WorkingDirectoryManager::instance()->workingDir()};

    CompanyInfosTable companyInfosTable{workingDir};

    auto *selfVatTable = new BookAccountSelfVatTable{
        workingDir, companyInfosTable.getCompanyCountryCode(), ui->tableAccounts};
    ui->tableAccounts->setModel(selfVatTable);
    ui->tableAccounts->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}

PaneSettingsSelfVatAccounts::~PaneSettingsSelfVatAccounts()
{
    delete ui;
}
