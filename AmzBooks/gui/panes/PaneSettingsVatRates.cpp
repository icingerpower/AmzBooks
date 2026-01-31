#include <QMessageBox>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include "books/VatResolver.h"

#include "gui/dialogs/DialogAddVatRate.h"

#include "PaneSettingsVatRates.h"
#include "ui_PaneSettingsVatRates.h"

PaneSettingsVatRates::PaneSettingsVatRates(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneSettingsVatRates)
{
    ui->setupUi(this);

    QDir workingDir{WorkingDirectoryManager::instance()->workingDir()};

    auto *vatResolver
        = new VatResolver{workingDir, ui->tableRates};
    ui->tableRates->setModel(vatResolver);

    _connectSlots();
}

void PaneSettingsVatRates::_connectSlots()
{
    connect(ui->buttonAdd,
            &QPushButton::clicked,
            this,
            &PaneSettingsVatRates::addRate);
    connect(ui->buttonRemove,
            &QPushButton::clicked,
            this,
            &PaneSettingsVatRates::removeRate);
}

PaneSettingsVatRates::~PaneSettingsVatRates()
{
    delete ui;
}


void PaneSettingsVatRates::addRate()
{
    DialogAddVatRate dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        VatResolver *resolver = static_cast<VatResolver *>(ui->tableRates->model());
        resolver->addRate(dialog.getDate(), dialog.getCountry(), dialog.getSaleType(), dialog.getRate(), dialog.getSpecialCode());
    }
}

void PaneSettingsVatRates::removeRate()
{
    const auto &selIndexes = ui->tableRates->selectionModel()->selectedIndexes();
    if (selIndexes.isEmpty()) {
        QMessageBox::warning(this, tr("Selection Required"), tr("Please select a rate to remove."));
        return;
    }

    if (QMessageBox::question(this, tr("Confirm Removal"), tr("Are you sure you want to remove the selected rate(s)?"), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        // Remove rows. Handle multiple selection.
        // Taking unique rows.
        QSet<int> rows;
        for (const QModelIndex &index : selIndexes) {
            rows.insert(index.row());
        }
        
        // Sort in descending order to remove from end
        QList<int> sortedRows = rows.values();
        std::sort(sortedRows.begin(), sortedRows.end(), std::greater<int>());
        
        VatResolver *resolver = static_cast<VatResolver *>(ui->tableRates->model());
        for (int row : sortedRows) {
            resolver->removeRow(row);
        }
    }
}
