#include <QMessageBox>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"
#include "books/SaleBookAccountsTable.h"
#include "books/ExceptionTaxSchemeInvalid.h"
#include "gui/dialogs/DialogAddSaleAccount.h"

#include "PaneSaleAccounts.h"
#include "ui_PaneSaleAccounts.h"

PaneSaleAccounts::PaneSaleAccounts(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PaneSaleAccounts)
{
    ui->setupUi(this);

    QDir workingDir{WorkingDirectoryManager::instance()->workingDir()};

    auto *saleAccountTable = new SaleBookAccountsTable{workingDir, this};
    ui->tableAccounts->setModel(saleAccountTable);

    _connectSlots();
}

PaneSaleAccounts::~PaneSaleAccounts()
{
    delete ui;
}

void PaneSaleAccounts::addRate()
{
    DialogAddSaleAccount dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        auto *model = qobject_cast<SaleBookAccountsTable*>(ui->tableAccounts->model());
        if (model) {
            try {
                VatCountries vc;
                vc.taxScheme = dialog.getTaxScheme();
                vc.countryCodeDeclaring = dialog.getCountryDeclaring();
                vc.countryCodeFrom = dialog.getCountryFrom();
                vc.countryCodeTo = dialog.getCountryTo();

                SaleBookAccountsTable::Accounts accounts;
                accounts.saleAccount = dialog.getSaleAccount();
                accounts.vatAccount = dialog.getVatAccount();
                accounts.vatAccountToPay = dialog.getVatAccountToPay();

                model->addAccount(vc, dialog.getVatRate(), accounts);
            } catch (const ExceptionTaxSchemeInvalid &e) {
                QMessageBox::warning(this, tr("Invalid Data"), e.errorTitle() + "\n" + e.errorText());
            } catch (const std::exception &e) {
                QMessageBox::warning(this, tr("Error"), e.what());
            }
        }
    }
}

void PaneSaleAccounts::removeRate()
{
    const auto &selIndexes = ui->tableAccounts->selectionModel()->selectedIndexes();
    if (selIndexes.isEmpty()) {
        QMessageBox::warning(this, tr("Selection Required"), tr("Please select an account to remove."));
        return;
    }

    if (QMessageBox::question(
                this
                , tr("Confirm Removal")
                , tr("Are you sure you want to remove the selected account(s)?")
                , QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        
        QSet<int> rows;
        for (const QModelIndex &index : selIndexes) {
            rows.insert(index.row());
        }
        
        QList<int> sortedRows = rows.values();
        std::sort(sortedRows.begin(), sortedRows.end(), std::greater<int>());
        
        auto *model = qobject_cast<SaleBookAccountsTable*>(ui->tableAccounts->model());
        if (model) {
            for (int row : sortedRows) {
                model->removeRow(row);
            }
        }
    }
}

void PaneSaleAccounts::_connectSlots()
{
    connect(ui->buttonAdd,
            &QPushButton::clicked,
            this,
            &PaneSaleAccounts::addRate);
    connect(ui->buttonRemove,
            &QPushButton::clicked,
            this,
            &PaneSaleAccounts::removeRate);
}
