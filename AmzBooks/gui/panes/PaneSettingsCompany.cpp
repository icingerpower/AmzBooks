#include <QMessageBox>

#include <CountriesEu.h>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include "books/CompanyInfosTable.h"
#include "books/CompanyAddressTable.h"
#include "books/VatNumbersTable.h"
#include "gui/dialogs/DialogAddVatNumber.h"
#include "gui/delegates/ComboBoxDelegate.h"

#include "PaneSettingsCompany.h"
#include "ui_PaneSettingsCompany.h"

PaneSettingsCompany::PaneSettingsCompany(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneSettingsCompany)
{
    ui->setupUi(this);

    QDir workingDir{WorkingDirectoryManager::instance()->workingDir()};

    auto *companyInfosTable
        = new CompanyInfosTable{workingDir, ui->tableCompanyInfos};
    ui->tableCompanyInfos->setModel(companyInfosTable);

    auto *companyAddressTable
        = new CompanyAddressTable{workingDir, ui->tableAddresses};
    ui->tableAddresses->setModel(companyAddressTable);

    auto *vatNumbersTable
        = new VatNumbersTable{workingDir, ui->tableVatNumbers};
    ui->tableVatNumbers->setModel(vatNumbersTable);

    // Setup Delegate for Country
    // We need to find the row index. The model is CompanyInfosTable.
    int countryRow = companyInfosTable->getRowById(CompanyInfosTable::ID_COUNTRY);
    if (countryRow != -1) {
        QStringList countries = CountriesEu::getCountries();
        auto *countryDelegate = new ComboBoxDelegate(countries, this);
        ui->tableCompanyInfos->setItemDelegateForRow(countryRow, countryDelegate);
    }

    // Setup Delegate for Currency
    int currencyRow = companyInfosTable->getRowById(CompanyInfosTable::ID_CURRENCY);
    if (currencyRow != -1) {
        // GB + EU Currencies
        QStringList currencies = CountriesEu::getCurrencies();
        auto *currencyDelegate = new ComboBoxDelegate(currencies, this);
        ui->tableCompanyInfos->setItemDelegateForRow(currencyRow, currencyDelegate);
    }

    _connectSlots();
}

PaneSettingsCompany::~PaneSettingsCompany()
{
    delete ui;
}

bool PaneSettingsCompany::hasVatNumberCompanyCountry() const
{
    auto *companyInfosTable = qobject_cast<CompanyInfosTable*>(ui->tableCompanyInfos->model());
    auto *vatNumbersTable = qobject_cast<VatNumbersTable*>(ui->tableVatNumbers->model());
    if (companyInfosTable && vatNumbersTable) {
        return vatNumbersTable->hasVatNumber(companyInfosTable->getCompanyCountryCode());
    }
    return false;
}

int PaneSettingsCompany::countVatNumbers() const
{
    return ui->tableVatNumbers->model()->rowCount();
}

int PaneSettingsCompany::countAddresses() const
{
    return ui->tableAddresses->model()->rowCount();
}

void PaneSettingsCompany::addVatNumbers()
{
    DialogAddVatNumber dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        const QString &country = dialog.getSelectedCountry();
        const QString &vatNumber = dialog.getVatNumber();
        
        VatNumbersTable *vatNumbersTable
            = static_cast<VatNumbersTable *>(
                ui->tableVatNumbers->model());
        
        try {
            vatNumbersTable->addVatNumber(country, vatNumber);
        } catch (const std::exception &e) {
            QMessageBox::warning(this, tr("Error"), tr("Failed to add VAT number: %1").arg(e.what()));
        }
    }
}

void PaneSettingsCompany::removeVatNumbers()
{
    auto *selectionModel = ui->tableVatNumbers->selectionModel();
    if (!selectionModel->hasSelection()) {
        QMessageBox::warning(this, tr("Selection Required"), tr("Please select a VAT number to remove."));
        return;
    }

    auto selectedRows = selectionModel->selectedRows();
    if (selectedRows.isEmpty()) {
         // Fallback if cell selection but not row selection, though QTableView usually selects rows if behavior is SelectRows
         // Let's assume we can get row from current index
         if (selectionModel->currentIndex().isValid()) {
             // Construct a list with one item for logic below
             // Use a different approach: just use currentIndex
             auto idx = selectionModel->currentIndex();
             selectedRows.append(idx); // Append a fake list item, but selectedRows returns QModelIndexList
         } else {
              return;
         }
    }
    
    // We only handle the first selected row for now to simplify logic
    QModelIndex index = selectedRows.first();

    auto *companyInfosTable = qobject_cast<CompanyInfosTable*>(ui->tableCompanyInfos->model());
    auto *vatNumbersTable = qobject_cast<VatNumbersTable*>(ui->tableVatNumbers->model());

    if (companyInfosTable && vatNumbersTable) {
        // Country is usually in column 0
        QModelIndex countryIndex = index.siblingAtColumn(0);
        QString country = countryIndex.data(Qt::DisplayRole).toString();

        if (country == companyInfosTable->getCompanyCountryCode()) {
            QMessageBox::warning(this, tr("Action Denied"), tr("You cannot remove the VAT number associated with the company's home country."));
            return;
        }

        vatNumbersTable->removeRow(index.row());
    }
}

void PaneSettingsCompany::addAddress()
{
    CompanyAddressTable *companyAddressTable
        = static_cast<CompanyAddressTable *>(
            ui->tableAddresses->model());
    companyAddressTable->insertRow(0);
}

void PaneSettingsCompany::removeAddress()
{
    const auto &selIndexes = ui->tableAddresses
                                 ->selectionModel()->selectedIndexes();
    if (selIndexes.size() > 0)
    {
        CompanyAddressTable *companyAddressTable
            = static_cast<CompanyAddressTable *>(
                ui->tableAddresses->model());
        companyAddressTable->remove(selIndexes.first());
    }
    else if (selIndexes.size() == 1)
    {
        QMessageBox::warning(this, tr("Address required"), tr("You need to keep at least one address."));
    }
    else
    {
        QMessageBox::warning(this, tr("Selection Required"), tr("Please select an address to remove."));
    }
}

void PaneSettingsCompany::_connectSlots()
{
    connect(ui->buttonAddAddress,
            &QPushButton::clicked,
            this,
            &PaneSettingsCompany::addAddress);
    connect(ui->buttonRemoveAddress,
            &QPushButton::clicked,
            this,
            &PaneSettingsCompany::removeAddress);
    connect(ui->buttonAddVatNumber,
            &QPushButton::clicked,
            this,
            &PaneSettingsCompany::addVatNumbers);
    connect(ui->buttonRemoveVatNumber,
            &QPushButton::clicked,
            this,
            &PaneSettingsCompany::removeVatNumbers);
}

