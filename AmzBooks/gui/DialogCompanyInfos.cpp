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
}

DialogCompanyInfos::~DialogCompanyInfos()
{
    delete ui;
}

void DialogCompanyInfos::accept()
{
    if (!ui->widgetInfos->hasVatNumberCompanyCountry())
    {
        QMessageBox::information(
            this,
            tr("No VAT number"),
            tr("You need to input the main VAT number of your company"));
        return;
    }
    if (ui->widgetInfos->countAddresses() == 0)
    {
        QMessageBox::information(
            this,
            tr("Your company address"),
            tr("You need to input at least one address for your company"));
        return;
    }
    QDialog::accept();
}
