#include <QMessageBox>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include "books/CompanyInfosTable.h"
#include "books/CompanyAddressTable.h"

#include "DialogCompanyInfos.h"
#include "ui_DialogCompanyInfos.h"

DialogCompanyInfos::DialogCompanyInfos(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogCompanyInfos)
{
    ui->setupUi(this);
    QDir workingDir{WorkingDirectoryManager::instance()->workingDir()};
    auto *companyInfosTable
        = new CompanyInfosTable{workingDir, ui->tableViewInfos};
    ui->tableViewInfos->setModel(companyInfosTable);
    auto *companyAddressTable
        = new CompanyAddressTable{workingDir, ui->tableViewAdresses};
    ui->tableViewAdresses->setModel(companyAddressTable);
    _connectSlots();
}

void DialogCompanyInfos::_connectSlots()
{
    connect(ui->buttonAdd,
            &QPushButton::clicked,
            this,
            &DialogCompanyInfos::addAddress);
    connect(ui->buttonRemove,
            &QPushButton::clicked,
            this,
            &DialogCompanyInfos::removeAddress);
}

DialogCompanyInfos::~DialogCompanyInfos()
{
    delete ui;
}

void DialogCompanyInfos::addAddress()
{
        CompanyAddressTable *companyAddressTable
            = static_cast<CompanyAddressTable *>(
                ui->tableViewAdresses->model());
    companyAddressTable->insertRow(0);
}

void DialogCompanyInfos::removeAddress()
{
    const auto &selIndexes = ui->tableViewAdresses
                                 ->selectionModel()->selectedIndexes();
    if (selIndexes.size() > 0)
    {
        CompanyAddressTable *companyAddressTable
            = static_cast<CompanyAddressTable *>(
                ui->tableViewAdresses->model());
        companyAddressTable->remove(selIndexes.first());
    }
}

void DialogCompanyInfos::accept()
{
    CompanyAddressTable *companyAddressTable
        = static_cast<CompanyAddressTable *>(
            ui->tableViewAdresses->model());
    if (companyAddressTable->rowCount() == 0)
    {
        QMessageBox::information(
            this,
            tr("No address"),
            tr("You need to input an address"));
        return;
    }
    QDialog::accept();
}
