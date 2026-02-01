#include <QMessageBox>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include "books/FbaCentersTable.h"
#include "gui/dialogs/DialogAddFbaCenter.h"

#include "PaneFbaCenters.h"
#include "ui_PaneFbaCenters.h"

PaneFbaCenters::PaneFbaCenters(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneFbaCenters)
{
    ui->setupUi(this);

    QDir workingDir{WorkingDirectoryManager::instance()->workingDir()};

    // Note: FbaCentersTable constructor takes workingDir
    auto *model = new FbaCentersTable{workingDir, ui->tableCenters};
    ui->tableCenters->setModel(model);

    _connectSlots();
}

void PaneFbaCenters::_connectSlots()
{
    connect(ui->buttonAdd,
            &QPushButton::clicked,
            this,
            &PaneFbaCenters::addCenter);
    connect(ui->buttonRemove,
            &QPushButton::clicked,
            this,
            &PaneFbaCenters::removeCenter);
}

PaneFbaCenters::~PaneFbaCenters()
{
    delete ui;
}

void PaneFbaCenters::addCenter()
{
    DialogAddFbaCenter dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        FbaCentersTable *model = static_cast<FbaCentersTable *>(ui->tableCenters->model());
        
        FbaCentersTable::FbaCenter center;
        center.centerId = dialog.getCenterId();
        center.countryCode = dialog.getCountryCode();
        center.postalCode = dialog.getPostalCode();
        center.city = dialog.getCity();
        
        if (center.centerId.isEmpty()) {
             QMessageBox::warning(this, tr("Invalid Input"), tr("Center ID is required."));
             return;
        }

        model->addCenter(center);
    }
}

void PaneFbaCenters::removeCenter()
{
    const auto &selIndexes = ui->tableCenters->selectionModel()->selectedIndexes();
    if (selIndexes.isEmpty()) {
        QMessageBox::warning(this, tr("Selection Required"), tr("Please select a center to remove."));
        return;
    }

    if (QMessageBox::question(
                this
                , tr("Confirm Removal")
                , tr("Are you sure you want to remove the selected center(s)?")
                , QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        
        QSet<int> rows;
        for (const QModelIndex &index : selIndexes) {
            rows.insert(index.row());
        }
        
        QList<int> sortedRows = rows.values();
        std::sort(sortedRows.begin(), sortedRows.end(), std::greater<int>());
        
        FbaCentersTable *model = static_cast<FbaCentersTable *>(ui->tableCenters->model());
        for (int row : sortedRows) {
            model->removeRow(row);
        }
    }
}
