#include "DialogEditCsvPurchases.h"
#include "ui_DialogEditCsvPurchases.h"
#include "profit/PurchaseFileSettingsTree.h"
#include <QInputDialog>
#include <QMessageBox>
#include "orders/ExceptionParamValue.h"

DialogEditCsvPurchases::DialogEditCsvPurchases(
        const QDir &workingDir, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogEditCsvPurchases)
{
    ui->setupUi(this);
    
    m_model = new PurchaseFileSettingsTree(workingDir, this);
    ui->treeView->setModel(m_model);
    ui->treeView->expandAll();

    _connectSlots();
}

DialogEditCsvPurchases::~DialogEditCsvPurchases()
{
    delete ui;
}

void DialogEditCsvPurchases::_connectSlots()
{
    connect(ui->buttonAdd, &QPushButton::clicked, this, &DialogEditCsvPurchases::addCandidate);
    connect(ui->buttonRemove, &QPushButton::clicked, this, &DialogEditCsvPurchases::removeCandidate);
}

void DialogEditCsvPurchases::addCandidate()
{
    QModelIndex index = ui->treeView->selectionModel()->currentIndex();
    if (!index.isValid()) {
        QMessageBox::warning(this, tr("Selection"), tr("Please select a category to add a candidate to."));
        return;
    }
    
    // Check if selected item is a fixed row (level 1)
    if (m_model->parent(index).isValid()) {
        // If child is selected, add to its parent
        index = m_model->parent(index);
    }
    
    bool ok;
    QString text = QInputDialog::getText(this, tr("Add Candidate"),
                                         tr("Column Name:"), QLineEdit::Normal,
                                         "", &ok);
    if (ok && !text.isEmpty()) {
        try {
            m_model->addCandidate(index, text);
            ui->treeView->expand(index);
        } catch (const ExceptionParamValue &e) {
            QMessageBox::warning(this, e.errorTitle(), e.errorText());
        }
    }
}

void DialogEditCsvPurchases::removeCandidate()
{
    QModelIndex index = ui->treeView->selectionModel()->currentIndex();
    if (!index.isValid()) return;
    
    // Only allow removing candidates (level 2)
    // Fixed rows have invalid parent (root) or logic check in model
    // In our model: parent(fixed_row) -> invalid. parent(candidate) -> fixed_row (valid)
    if (!m_model->parent(index).isValid()) {
        QMessageBox::warning(this, tr("Remove"), tr("You cannot remove fixed categories."));
        return;
    }
    
    m_model->removeRow(index.row(), m_model->parent(index));
}
