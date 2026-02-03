#include <QDateTime>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include "books/AbstractBooksTableBank.h"
#include "books/BooksConnections.h"
#include "banks/AbstractBankStatement.h"
#include "books/PurchaseInvoiceTable.h"

#include <QTableView>
#include <QHeaderView>

#include "PaneBookKeeping.h"
#include "ui_PaneBookKeeping.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QFileInfo>

#include "../../common/utils/CsvHeader.h"
#include "books/ExceptionFileError.h"
#include "../dialogs/DialogPurchaseInvoices.h"

// For confirmation message box
#include <QMessageBox>

PaneBookKeeping::PaneBookKeeping(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PaneBookKeeping)
{
    ui->setupUi(this);
    m_booksConnections = new BooksConnections{
            WorkingDirectoryManager::instance()->workingDir()};

    _initYears();
    _createBooksTables();
    _setSubButtonsEnabled(false);
    _connectSlots();
}

PaneBookKeeping::~PaneBookKeeping()
{
    delete ui;
    delete m_booksConnections;
}

void PaneBookKeeping::loadYearSelected()
{
    _setSubButtonsEnabled(true);
}

void PaneBookKeeping::generateBookKeeping()
{

}

void PaneBookKeeping::unselectAll()
{
    QList<QTableView *> allViews = this->findChildren<QTableView *>();
    for (auto &view : allViews)
    {
        view->clearSelection();
    }
}

void PaneBookKeeping::associate()
{

}

void PaneBookKeeping::dissociate()
{

}

void PaneBookKeeping::purchaseAdd()
{
    QSettings settings;
    QString lastDir = settings.value("lastPurchaseDir", QDir::homePath()).toString();
    
    QString fileName = QFileDialog::getOpenFileName(this, 
                                                    tr("Select Purchase Invoice"), 
                                                    lastDir, 
                                                    tr("Invoice Files (*.pdf *.png *.jpg *.jpeg)"));
    
    if (fileName.isEmpty()) {
        return;
    }
    
    settings.setValue("lastPurchaseDir", QFileInfo(fileName).absolutePath());
    
    auto purchaseTable = static_cast<PurchaseInvoiceTable *>(ui->tableInvoices->model());
    
    try {
        QFileInfo fi(fileName);
        PurchaseInformation info = PurchaseInvoiceManager::decode(fi.fileName());
        // We pass the full path so that add() can copy it.
        // But add() takes sourceFilePath and info.
        // Info.filePath might be set by decode() if we passed full path but decode() only looks at fileName usually.
        // Let's rely on info structure from decode, and add just needs correct info date/etc.
        
        purchaseTable->manager().add(fileName, info);
        // Reload table for the year of the invoice
        purchaseTable->load(info.date.year());
        
        // Also ensure UI year is correct? Or just warn if added to a different year?
        if (ui->comboBoxYear->currentText().toInt() != info.date.year()) {
            QMessageBox::information(this, tr("Invoice Added"), 
                tr("Invoice added to year %1 (Current view is %2). Switch year to view it.")
                .arg(info.date.year())
                .arg(ui->comboBoxYear->currentText()));
        } else {
             // Refresh current view
             purchaseTable->load(info.date.year());
        }

    } catch (const ExceptionFileError &e) {
        QMessageBox::warning(this, e.errorTitle(), e.errorText());
    } catch (...) {
        QMessageBox::warning(this, tr("Error"), tr("Unknown error adding invoice."));
    }
}

void PaneBookKeeping::purchaseAddMany()
{
    QSettings settings;
    QString lastDir = settings.value("lastPurchaseDir", QDir::homePath()).toString();
    
    QStringList fileNames = QFileDialog::getOpenFileNames(this, 
                                                          tr("Select Purchase Invoices"), 
                                                          lastDir, 
                                                          tr("Invoice Files (*.pdf *.png *.jpg *.jpeg)"));
    
    if (fileNames.isEmpty()) {
        return;
    }
    
    settings.setValue("lastPurchaseDir", QFileInfo(fileNames.first()).absolutePath());
    
    DialogPurchaseInvoices dialog(fileNames, this);
    if (dialog.exec() == QDialog::Accepted) {
        auto purchaseTable = static_cast<PurchaseInvoiceTable *>(ui->tableInvoices->model());
        QList<PurchaseInformation> invoicesToAdd = dialog.selectedInvoices();
        
        int count = 0;
        int errCount = 0;
        
        for (PurchaseInformation info : invoicesToAdd) {
            try {
                // Here info.filePath is the source file path (set in dialog)
                purchaseTable->manager().add(info.filePath, info);
                count++;
            } catch (...) {
                errCount++;
            }
        }
        
        // Reload current view
        purchaseTable->load(ui->comboBoxYear->currentText().toInt());
        
        QString msg = tr("Added %1 invoices.").arg(count);
        if (errCount > 0) {
            msg += tr("\n%1 errors occurred.").arg(errCount);
        }
        QMessageBox::information(this, tr("Import Result"), msg);
    }
}

void PaneBookKeeping::purchaseRemove()
{
    auto purchaseTable = static_cast<PurchaseInvoiceTable *>(ui->tableInvoices->model());
    QModelIndexList selection = ui->tableInvoices->selectionModel()->selectedRows();
    
    if (selection.isEmpty()) {
        QMessageBox::warning(this, tr("No Selection"), tr("Please select invoices to remove."));
        return;
    }
    
    if (QMessageBox::question(this, tr("Confirm Removal"), 
                              tr("Are you sure you want to remove %1 invoices? "
                                 "This will delete the files from disk.")
                              .arg(selection.size())) 
        != QMessageBox::Yes) {
        return;
    }
    
    // Sort selection reverse to remove safely? 
    // removeInvoice uses rowId and manager removal, so index validity changes are tricky if we rely on indices.
    // However, PurchaseInvoiceTable::removeInvoice takes a QModelIndex.
    // If we remove one, indices shift.
    // Better strategy: Collect Row IDs first.
    
    QStringList rowIds;
    for (const QModelIndex &idx : selection) {
        rowIds << purchaseTable->getRowId(idx);
    }
    
    for (const QString &id : rowIds) {
        purchaseTable->manager().remove(id);
    }
    
    // Reload to refresh view properly (or trust manager remove logic if it was robust enough for batch)
    // PurchaseInvoiceTable::load clears and reloads.
    purchaseTable->load(ui->comboBoxYear->currentText().toInt());
}

void PaneBookKeeping::bankAdd()
{
    AbstractBooksTableBank *bankTable = getVisibleBankTable();
    if (!bankTable) {
        QMessageBox::warning(this, tr("No Bank Selected"), tr("Please select a bank first."));
        return;
    }

    const AbstractBankStatement *stmt = bankTable->getBankStatement();
    if (!stmt)
    {
        return;
    }

    QSettings settings;
    QString lastDir = settings.value("lastBankDir", QDir::homePath()).toString();
    QString filters = stmt->fileFilters().join(";;");

    QString fileName = QFileDialog::getOpenFileName(this, tr("Select Bank File"), lastDir, filters);
    
    if (fileName.isEmpty())
    {
        return;
    }

    QString dir = QFileInfo(fileName).absolutePath();
    settings.setValue("lastBankDir", dir);

    try {
        bankTable->addFilePaths({fileName});
    } catch (const CsvHeaderException &e) {
        QMessageBox::warning(this, tr("Import Error"), tr("CSV Header Error: Missing columns %1").arg(e.columnValuesError().join(", ")));
    } catch (const ExceptionFileError &e) {
        QMessageBox::warning(this, e.errorTitle(), e.errorText());
    } catch (const std::exception &e) {
        QMessageBox::warning(this, tr("Import Error"), tr("Error importing file: %1").arg(e.what()));
    } catch (...) {
        QMessageBox::warning(this, tr("Import Error"), tr("Unknown error importing file."));
    }
}

void PaneBookKeeping::bankRemove()
{
    AbstractBooksTableBank *bankTable = getVisibleBankTable();
    if (!bankTable)
    {
        return;
    }

    QTableView *view = qobject_cast<QTableView*>(ui->toolBoxBanks->currentWidget());
    if (!view)
    {
        return;
    }

    QModelIndexList selection = view->selectionModel()->selectedRows();
    if (selection.isEmpty()) {
        QMessageBox::warning(this, tr("No Selection"), tr("Please select rows to remove."));
        return;
    }

    if (QMessageBox::question(this, tr("Confirm Removal"), 
                              tr("Are you sure you want to remove the selected rows? "
                                 "This will delete the associated files from disk.")) 
        != QMessageBox::Yes) {
        return;
    }

    try {
        bankTable->remove(selection);
    } catch (const std::exception &e) {
        QMessageBox::warning(this, tr("Remove Error"), tr("Error removing files: %1").arg(e.what()));
    }
}

void PaneBookKeeping::_createBanks()
{
    // Remove existing pages
    while (ui->toolBoxBanks->count() > 0) {
        QWidget *widget = ui->toolBoxBanks->widget(0);
        ui->toolBoxBanks->removeItem(0);
        delete widget;
    }

    // Browse AbstractBooksTableBank::ALL_TABLES()
    const auto &tables = AbstractBooksTableBank::ALL_TABLES();
    for (auto it = tables.begin(); it != tables.end(); ++it) {
        
        // Create Table View
        QTableView *view = new QTableView(this);
        // Create table model
        AbstractBooksTableBank *bankTable = it.value()(m_booksConnections,
                                                       WorkingDirectoryManager::instance()->workingDir(),
                                                       view);
        view->setModel(bankTable);
        view->setAlternatingRowColors(true);
        view->horizontalHeader()->setStretchLastSection(true);
        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        view->setSelectionMode(QAbstractItemView::ExtendedSelection);

        // Add to toolBox
        QString name = bankTable->getBankStatement() ? bankTable->getBankStatement()->getName() : "Unknown";
        ui->toolBoxBanks->addItem(view, name);
    }
}

void PaneBookKeeping::_createBooksTables()
{
    _createBanks();
    QDir workingDir{WorkingDirectoryManager::instance()->workingDir()};
    auto purchaseTable = new PurchaseInvoiceTable(m_booksConnections, workingDir, ui->tableInvoices);
    ui->tableInvoices->setModel(purchaseTable);
}

void PaneBookKeeping::_initYears()
{
    int currentYear = QDate::currentDate().year();
    for (int i=0; i<5; ++i) {
        ui->comboBoxYear->addItem(QString::number(currentYear - i));
    }
}

void PaneBookKeeping::_connectSlots()
{
    connect(ui->buttonLoadYear,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::loadYearSelected);
    connect(ui->buttonGenBooks,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::generateBookKeeping);
    connect(ui->buttonUnselectAll,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::unselectAll);

    connect(ui->buttonAssociate,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::associate);
    connect(ui->buttonDissociate,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::dissociate);

    connect(ui->buttonInvoiceAdd,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::purchaseAdd);
    connect(ui->buttonInvoiceAddMany,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::purchaseAddMany);
    connect(ui->buttonInvoiceRemove,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::purchaseRemove);

    connect(ui->buttonBankAddStatement,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::bankAdd);
    connect(ui->buttonBankRemove,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::bankRemove);
}

void PaneBookKeeping::_setSubButtonsEnabled(bool enabled)
{
    // Browse every QPushButton of the main class
    QList<QPushButton *> allButtons = this->findChildren<QPushButton *>();
    
    for (QPushButton *btn : allButtons) {
        if (btn != ui->buttonLoadYear)
        {
            // It is NOT in the main list, so we enable/disable it
            btn->setEnabled(enabled);
        }
    }
}

AbstractBooksTableBank *PaneBookKeeping::getVisibleBankTable() const
{
    QWidget *currentWidget = ui->toolBoxBanks->currentWidget();
    if (auto *view = qobject_cast<QTableView*>(currentWidget)) {
        return qobject_cast<AbstractBooksTableBank*>(view->model());
    }
    return nullptr;
}
