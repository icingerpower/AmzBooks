#include <QMessageBox>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include "books/CompanyInfosTable.h"
#include "books/CompanyAddressTable.h"
#include "gui/panes/WidgetPurchases.h"

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

bool DialogCompanyInfos::isInitialized() const
{
    return ui->widgetInfos->getWidgetPurchases()->isInitialized();
}

void DialogCompanyInfos::accept()
{
    if (!ui->widgetInfos->getWidgetPurchases()->isInitialized())
    {
        QMessageBox::information(
            this,
            tr("Purchase folders missing"),
            tr("You need to select the 2 purchase folders"));
        return;
    }
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
