#include <QMessageBox>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"
#include "books/BooksAccountsSalesTable.h"
#include "books/ExceptionTaxSchemeInvalid.h"
#include "gui/dialogs/DialogAddSaleAccount.h"

#include "PaneSettingsSaleAccounts.h"
#include "ui_PaneSettingsSaleAccounts.h"

PaneSettingsSaleAccounts::PaneSettingsSaleAccounts(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PaneSettingsSaleAccounts)
{
    ui->setupUi(this);

    QDir workingDir{WorkingDirectoryManager::instance()->workingDir()};

    auto *saleAccountTable = new BooksAccountsSalesTable{workingDir, this};
    ui->tableAccounts->setModel(saleAccountTable);

    _connectSlots();
}

PaneSettingsSaleAccounts::~PaneSettingsSaleAccounts()
{
    delete ui;
}

void PaneSettingsSaleAccounts::addRate()
{
    DialogAddSaleAccount dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        auto *model = qobject_cast<BooksAccountsSalesTable*>(ui->tableAccounts->model());
        if (model) {
            try {
                VatCountries vc;
                vc.taxScheme = dialog.getTaxScheme();
                vc.countryCodeDeclaring = dialog.getCountryDeclaring();
                vc.countryCodeFrom = dialog.getCountryFrom();
                vc.countryCodeTo = dialog.getCountryTo();

                BooksAccountsSalesTable::Accounts accounts;
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

void PaneSettingsSaleAccounts::removeRate()
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
        
        auto *model = qobject_cast<BooksAccountsSalesTable*>(ui->tableAccounts->model());
        if (model) {
            for (int row : sortedRows) {
                model->removeRow(row);
            }
        }
    }
}

void PaneSettingsSaleAccounts::_connectSlots()
{
    connect(ui->buttonAdd,
            &QPushButton::clicked,
            this,
            &PaneSettingsSaleAccounts::addRate);
    connect(ui->buttonRemove,
            &QPushButton::clicked,
            this,
            &PaneSettingsSaleAccounts::removeRate);
}
