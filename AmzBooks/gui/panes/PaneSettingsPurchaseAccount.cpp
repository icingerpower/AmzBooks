#include <QMessageBox>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include "books/BookAccountPurchaseTable.h"
#include "books/CompanyInfosTable.h"
#include "ExceptionWithTitleText.h"
#include "PaneSettingsPurchaseAccount.h"
#include "ui_PaneSettingsPurchaseAccount.h"
#include "gui/dialogs/DialogAddPurchaseAccount.h"

PaneSettingsPurchaseAccount::PaneSettingsPurchaseAccount(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PaneSettingsPurchaseAccount)
{
    ui->setupUi(this);

    QDir workingDir{WorkingDirectoryManager::instance()->workingDir()};

    CompanyInfosTable companyInfosTable{workingDir};

    auto *purchaseAccountTable
        = new BookAccountPurchaseTable{
            workingDir, companyInfosTable.getCompanyCountryCode(), ui->tableAccounts};
    ui->tableAccounts->setModel(purchaseAccountTable);

    _connectSlots();
}

PaneSettingsPurchaseAccount::~PaneSettingsPurchaseAccount()
{
    delete ui;
}

void PaneSettingsPurchaseAccount::addRate()
{
    DialogAddPurchaseAccount dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        auto *model = qobject_cast<BookAccountPurchaseTable*>(ui->tableAccounts->model());
        if (model) {
            try {
                model->addAccount(dialog.getCountry(),
                                  dialog.getVatRate(),
                                  dialog.getAccountDebit6(),
                                  dialog.getAccountCredit4());
            } catch (const ExceptionWithTitleText &e) {
                QMessageBox::warning(this, tr("Invalid Data"), e.errorTitle() + "\n" + e.errorText());
            } catch (const std::exception &e) {
                QMessageBox::warning(this, tr("Error"), e.what());
            }
        }
    }
}

void PaneSettingsPurchaseAccount::removeRate()
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

        // Remove rows. Handle multiple selection.
        QSet<int> rows;
        for (const QModelIndex &index : selIndexes) {
            rows.insert(index.row());
        }

        // Sort in descending order to remove from end
        QList<int> sortedRows = rows.values();
        std::sort(sortedRows.begin(), sortedRows.end(), std::greater<int>());

        auto *model = qobject_cast<BookAccountPurchaseTable*>(ui->tableAccounts->model());
        if (model) {
            for (int row : sortedRows) {
                model->removeRow(row);
            }
        }
    }
}

void PaneSettingsPurchaseAccount::_connectSlots()
{
    connect(ui->buttonAdd,
            &QPushButton::clicked,
            this,
            &PaneSettingsPurchaseAccount::addRate);
    connect(ui->buttonRemove,
            &QPushButton::clicked,
            this,
            &PaneSettingsPurchaseAccount::removeRate);
}
