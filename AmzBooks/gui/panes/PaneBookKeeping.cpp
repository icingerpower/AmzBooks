#include <QDateTime>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include "books/AbstractBooksTableBank.h"
#include "books/EntrySelfTable.h"
#include "books/BooksConnections.h"
#include "banks/AbstractBankStatement.h"
#include "books/PurchaseInvoiceTable.h"

#include <QTableView>
#include <QHeaderView>

#include "CurrencyRateManager.h"

#include "PaneBookKeeping.h"
#include "ui_PaneBookKeeping.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QFileInfo>
#include <QSet>

#include "../../common/utils/CsvHeader.h"
#include "books/ExceptionFileError.h"
#include "../dialogs/DialogPurchaseInvoices.h"
#include "gui/dialogs/DialogEditServiceClients.h"
#include "gui/dialogs/DialogAddSaleService.h"
#include "books/ServiceSalesBooksTable.h"
#include "books/ServiceClientManager.h"
#include "books/CompanyInfosTable.h"
#include "orders/OrderManager.h"

// For confirmation message box
#include <QMessageBox>

PaneBookKeeping::PaneBookKeeping(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PaneBookKeeping)
{
    ui->setupUi(this);
    QDir workingDir(WorkingDirectoryManager::instance()->workingDir());
    m_booksConnections = new BooksConnections{workingDir};
    m_orderManager = new OrderManager(workingDir);

    _initYears();
    _createBooksTables();
    _setSubButtonsEnabled(false);
    _connectSlots();
}

PaneBookKeeping::~PaneBookKeeping()
{
    delete ui;
    delete m_booksConnections;
    delete m_orderManager;
}

void PaneBookKeeping::loadYearSelected()
{
    _setSubButtonsEnabled(true);
    // TODO AbstractBooksTable::load for all getAllBankTables and getAllNonBankTables
}

void PaneBookKeeping::generateBookKeeping()
{
    // TODO ask for a save folder (suggesting the last folder choose according QSettings{})
    // TODO call BooksConnections::associateTablesToIds for all getAllBookTables
    // TODO then using JournalTable / JournalEntryFactory create const QHash<QString, QMultiMap<QDate, QSharedPointer<JournalEntry>>> &journal_date_entries
    // TODO then with BookSaverFull save in the choosen folder
    // TODO with InvoiceGenerator save also in the choosen folder, oll of the invoices into invoices folder (one folder per year / month)
    // TODO catch excetpion or display a QMessageBox confirmation message if successful
    // TODO if needed a callback create DialogVatParams and returning true if dialog is accepted
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
    // Get the visible bank table
    AbstractBooksTableBank *bankTable = getVisibleBankTable();
    if (!bankTable) {
        QMessageBox::warning(this, tr("No Bank Selected"), 
            tr("Please select a bank account tab first."));
        return;
    }

    // Get the bank table view
    QTableView *bankView = qobject_cast<QTableView*>(ui->toolBoxBanks->currentWidget());
    if (!bankView) {
        QMessageBox::warning(this, tr("No Bank View"), 
            tr("Unable to access the bank table view."));
        return;
    }

    // Get selection from bank table
    QModelIndexList bankSelection = bankView->selectionModel()->selectedRows();
    if (bankSelection.isEmpty()) {
        QMessageBox::warning(this, tr("No Bank Selection"), 
            tr("Please select at least one row from the bank table."));
        return;
    }

    // Get selection from self-entry table
    QModelIndexList selfSelection = ui->tableSelfEntry->selectionModel()->selectedRows();

    // If self-selection is NOT empty, use the specialized self-entry association
    if (!selfSelection.isEmpty()) {
        // Get the self-entry table
        EntrySelfTable *selfTable = const_cast<EntrySelfTable*>(getSeflEntryTable());
        if (!selfTable) {
            QMessageBox::warning(this, tr("No Self Entry Table"), 
                tr("Unable to access the self-entry table."));
            return;
        }

        // According to the task: "Associate EntrySelfTable only one row to AbstractBooksTableBank"
        // This means each bank row can only be associated with ONE self-entry row
        // But several rows of same bank instance possible (one self-entry to multiple bank rows)

        // For each selected bank row, associate with the first selected self-entry row
        // (This enforces the "one row" constraint from self-entry to bank)
        if (selfSelection.size() > 1) {
            QMessageBox::information(this, tr("Multiple Self Entries Selected"), 
                tr("Only the first selected self-entry row will be used for association. "
                   "Each bank row can only be associated with one self-entry row."));
        }

        const QModelIndex &selfIndex = selfSelection.first();

        // Perform associations using BooksConnections
        try {
            m_booksConnections->tryToConnect(bankTable, bankSelection, selfTable, selfIndex);
            QMessageBox::information(this, tr("Association Successful"), 
                tr("Successfully associated %1 bank row(s) with the selected self-entry.")
                .arg(bankSelection.size()));
        } catch (const std::exception &e) {
            QMessageBox::warning(this, tr("Association Failed"), 
                tr("Failed to associate entries: %1").arg(e.what()));
        }
    }
    // If self-selection is empty, use the hash-based tryToConnect with book tables
    else {
        // Get all non-bank, non-self tables (purchase, services, etc.)
        QList<const AbstractBooksTable *> bookTables = getAllNonBankTables();
        
        if (bookTables.isEmpty()) {
            QMessageBox::warning(this, tr("No Book Tables"), 
                tr("No book tables available for association."));
            return;
        }

        // Build the hash of tables to their selections
        QHash<AbstractBooksTable *, QModelIndexList> tableIndexes;
        
        // Add bank table with its selection
        tableIndexes.insert(bankTable, bankSelection);
        
        // For each book table, get its selection
        for (const AbstractBooksTable *bookTable : bookTables) {
            // Find the QTableView for this book table
            QList<QTableView *> allViews = ui->toolBoxSalePurchases->findChildren<QTableView *>();
            for (QTableView *view : allViews) {
                if (view->model() == bookTable) {
                    QModelIndexList selection = view->selectionModel()->selectedRows();
                    if (!selection.isEmpty()) {
                        AbstractBooksTable *nonConstTable = const_cast<AbstractBooksTable *>(bookTable);
                        tableIndexes.insert(nonConstTable, selection);
                    }
                    break;
                }
            }
        }

        // Check if we have at least two tables with selections
        if (tableIndexes.size() < 2) {
            QMessageBox::warning(this, tr("Insufficient Selection"), 
                tr("Please select rows from at least two tables (bank and book tables) to associate."));
            return;
        }

        // Create CurrencyRateManager for the connection
        QDir workingDir = WorkingDirectoryManager::instance()->workingDir();
        CompanyInfosTable companyInfo{workingDir};
        const auto &apiKey = companyInfo.getApiKeyFixer();
        if (apiKey.isEmpty())
        {
            QMessageBox::warning(
                        this
                        , tr("Fixer.io API key is empty")
                        , tr("Fixer.io API key can't be empty. You need to create an account and enter the API key in the settings."));
        }
        else
        {
            CurrencyRateManager currencyRateManager{workingDir, apiKey};

            // Perform associations using the hash-based tryToConnect
            try {
                m_booksConnections->tryToConnect(tableIndexes, &currencyRateManager);
                QMessageBox::information(this, tr("Association Successful"),
                                         tr("Successfully associated entries between selected tables."));
            } catch (const std::exception &e) {
                QMessageBox::warning(this, tr("Association Failed"),
                                     tr("Failed to associate entries: %1").arg(e.what()));
            }
        }
    }
}

void PaneBookKeeping::dissociate()
{
    // Collect all selections from book and bank tables
    QHash<AbstractBooksTable *, QModelIndexList> tableSelections;
    
    // Get selection from bank tables
    AbstractBooksTableBank *bankTable = getVisibleBankTable();
    if (bankTable) {
        QTableView *bankView = qobject_cast<QTableView*>(ui->toolBoxBanks->currentWidget());
        if (bankView) {
            QModelIndexList bankSelection = bankView->selectionModel()->selectedRows();
            if (!bankSelection.isEmpty()) {
                tableSelections.insert(bankTable, bankSelection);
            }
        }
    }
    
    // Get selection from self-entry table (handled separately)
    EntrySelfTable *selfTable = const_cast<EntrySelfTable*>(getSeflEntryTable());
    QModelIndexList selfSelection;
    if (selfTable) {
        selfSelection = ui->tableSelfEntry->selectionModel()->selectedRows();
    }
    
    // Get selection from book tables (purchases, services, etc.)
    QList<const AbstractBooksTable *> bookTables = getAllNonBankTables();
    for (const AbstractBooksTable *bookTable : bookTables) {
        QList<QTableView *> allViews = ui->toolBoxSalePurchases->findChildren<QTableView *>();
        for (QTableView *view : allViews) {
            if (view->model() == bookTable) {
                QModelIndexList selection = view->selectionModel()->selectedRows();
                if (!selection.isEmpty()) {
                    AbstractBooksTable *nonConstTable = const_cast<AbstractBooksTable *>(bookTable);
                    tableSelections.insert(nonConstTable, selection);
                }
                break;
            }
        }
    }
    
    // Check if any selection exists
    if (tableSelections.isEmpty() && selfSelection.isEmpty()) {
        QMessageBox::warning(this, tr("No Selection"), 
            tr("Please select at least one row from any table to dissociate."));
        return;
    }
    
    // Check if any of the selected rows have connections
    bool hasConnections = false;
    int totalRowsToDisconnect = 0;
    
    // Check book/bank tables
    for (auto it = tableSelections.begin(); it != tableSelections.end(); ++it) {
        AbstractBooksTable *table = it.key();
        const QModelIndexList &indices = it.value();
        
        for (const QModelIndex &index : indices) {
            QString rowId = table->getRowId(index);
            if (m_booksConnections->contains(table->getId(), rowId)) {
                hasConnections = true;
                totalRowsToDisconnect++;
            }
        }
    }
    
    // Check self-entry table
    if (selfTable && !selfSelection.isEmpty()) {
        for (const QModelIndex &index : selfSelection) {
            QString rowId = selfTable->getRowId(index);
            if (m_booksConnections->contains(selfTable->getId(), rowId)) {
                hasConnections = true;
                totalRowsToDisconnect++;
            }
        }
    }
    
    // If no connections found, warn the user
    if (!hasConnections) {
        QMessageBox::warning(this, tr("No Connections"), 
            tr("None of the selected rows have any connections to dissociate."));
        return;
    }
    
    // Perform disconnection for all selected rows that have connections
    int disconnectedCount = 0;
    
    // Disconnect book/bank tables
    for (auto it = tableSelections.begin(); it != tableSelections.end(); ++it) {
        AbstractBooksTable *table = it.key();
        const QModelIndexList &indices = it.value();
        
        for (const QModelIndex &index : indices) {
            QString rowId = table->getRowId(index);
            if (m_booksConnections->contains(table->getId(), rowId)) {
                m_booksConnections->disconnect(table, index);
                disconnectedCount++;
            }
        }
    }
    
    // Disconnect self-entry table
    if (selfTable && !selfSelection.isEmpty()) {
        for (const QModelIndex &index : selfSelection) {
            QString rowId = selfTable->getRowId(index);
            if (m_booksConnections->contains(selfTable->getId(), rowId)) {
                m_booksConnections->disconnect(selfTable, index);
                disconnectedCount++;
            }
        }
    }
    
    // Refresh all views to update the background colors
    if (bankTable) {
        QTableView *bankView = qobject_cast<QTableView*>(ui->toolBoxBanks->currentWidget());
        if (bankView) {
            bankView->viewport()->update();
        }
    }
    if (selfTable) {
        ui->tableSelfEntry->viewport()->update();
    }
    for (const AbstractBooksTable *bookTable : bookTables) {
        QList<QTableView *> allViews = ui->toolBoxSalePurchases->findChildren<QTableView *>();
        for (QTableView *view : allViews) {
            if (view->model() == bookTable) {
                view->viewport()->update();
                break;
            }
        }
    }
    
    QMessageBox::information(this, tr("Dissociation Successful"), 
        tr("Successfully dissociated %1 row(s).").arg(disconnectedCount));
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

void PaneBookKeeping::serviceAddSale()
{
    // Access ServiceSalesBooksTable
    auto *serviceTable = static_cast<ServiceSalesBooksTable *>(ui->tableServices->model());
    if (!serviceTable) {
        return;
    }

    ServiceClientManager clientManager(WorkingDirectoryManager::instance()->workingDir());
    
    DialogAddSaleService dialog(&clientManager, this);
    if (dialog.exec() == QDialog::Accepted) {
        serviceTable->createSale(
            &clientManager,
            dialog.getSelectedClientRow(),
            dialog.getDate(),
            dialog.getAmount(),
            dialog.getCurrency(),
            dialog.getInvoiceId()
        );
    }
}

void PaneBookKeeping::serviceRemoveSale()
{
    auto *serviceTable = static_cast<ServiceSalesBooksTable *>(ui->tableServices->model());
    if (!serviceTable) return;

    QModelIndexList selection = ui->tableServices->selectionModel()->selectedIndexes();
    QSet<int> rows;
    for (const auto &idx : selection) {
        rows.insert(idx.row());
    }

    if (rows.isEmpty()) {
        QMessageBox::warning(this, tr("No Selection"), tr("Please select sales to remove."));
        return;
    }

    if (QMessageBox::question(this, tr("Confirm Removal"),
                              tr("Are you sure you want to remove %1 sales?").arg(rows.size()))
        != QMessageBox::Yes) {
        return;
    }

    // Remove logic
    // Collect IDs first to identify rows because indices change
    // ServiceSalesBooksTable inherits AbstractBooksTable which has getRowId(idx)
    QStringList ids;
    for(int row : rows) {
        ids << serviceTable->getRowId(serviceTable->index(row, 0));
    }
    
    for(const QString &id : ids) {
        serviceTable->remove(id);
    }
}

void PaneBookKeeping::serviceEditClients()
{
    ServiceClientManager clientManager(WorkingDirectoryManager::instance()->workingDir());
    DialogEditServiceClients dialog(&clientManager, this);
    dialog.exec();
    // Clients saved automatically by manager on change/destruct
}

void PaneBookKeeping::serviceCreateFromSelection()
{
    // 1. Check selection in visible bank table
    AbstractBooksTableBank *bankTable = getVisibleBankTable();
    if (!bankTable) {
        QMessageBox::warning(this, tr("Selection Error"), tr("Please select a bank account tab first."));
        return;
    }
    // Check selection from toolBoxBanks current item
    QTableView *view = qobject_cast<QTableView *>(ui->toolBoxBanks->currentWidget());
    if (!view) return;
    
    QModelIndexList selection = view->selectionModel()->selectedIndexes();
    QSet<int> rows;
    for (const auto &idx : selection) {
        rows.insert(idx.row());
    }

    if (rows.isEmpty()) {
        QMessageBox::warning(this, tr("Selection Error"), tr("Please select at least one bank transaction line."));
        return;
    }
    
    double totalAmount = 0.0;
    QString currency;
    QStringList labels;
    QDate date; 
    
    bool first = true;
    for (int row : rows) {
        if (first) {
            date = bankTable->getDate(row);
            currency = bankTable->getCurrency(row);
            first = false;
        } else {
            if (bankTable->getCurrency(row) != currency) {
                 QMessageBox::warning(this, tr("Selection Error"), tr("Selected lines have different currencies."));
                 return;
            }
        }
        totalAmount += bankTable->getAmount(row);
        labels << bankTable->getLabel(row);
    }
    
    // Create the sale dialog with pre-filled data
    ServiceClientManager clientManager(WorkingDirectoryManager::instance()->workingDir());
    DialogAddSaleService dialog(&clientManager, this);
    
    dialog.setAmount(qAbs(totalAmount)); 
    // dialog.setCurrency(currency); // Currency determined by client
    dialog.setReference(labels.join(" + "));
    dialog.setDate(date);
    
    if (dialog.exec() == QDialog::Accepted) {
        auto *serviceTable = static_cast<ServiceSalesBooksTable *>(ui->tableServices->model());
        if(serviceTable) { 
            serviceTable->createSale(
                &clientManager,
                dialog.getSelectedClientRow(),
                dialog.getDate(),
                dialog.getAmount(),
                dialog.getCurrency(),
                dialog.getInvoiceId()
            );
        }
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
    
    // Purchases
    auto purchaseTable = new PurchaseInvoiceTable(m_booksConnections, workingDir, ui->tableInvoices);
    ui->tableInvoices->setModel(purchaseTable);
    
    // Services
    auto serviceTable = new ServiceSalesBooksTable(m_booksConnections, m_orderManager, workingDir, ui->tableServices);
    ui->tableServices->setModel(serviceTable);
    ui->tableServices->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableServices->setSelectionMode(QAbstractItemView::ExtendedSelection);
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

const EntrySelfTable *PaneBookKeeping::getSeflEntryTable() const
{
    return static_cast<const EntrySelfTable *>(ui->tableSelfEntry->model());
}

QList<const AbstractBooksTable *> PaneBookKeeping::getAllBookTables() const
{
    auto allBookTables = getAllNonBankTables();
    auto allBankTables = getAllBankTables();
    for (auto bankTable : allBankTables)
    {
        allBookTables << bankTable;
    }
    return allBookTables;
}

QList<const AbstractBooksTableBank *> PaneBookKeeping::getAllBankTables() const
{
    QList<const AbstractBooksTableBank *> bankTables;
    QList<QTableView *> allViews = ui->toolBoxBanks->findChildren<QTableView *>();
    for (auto view : allViews)
    {
        bankTables << static_cast<const AbstractBooksTableBank *>(view->model());
    }
    return bankTables;
}

QList<const AbstractBooksTable *> PaneBookKeeping::getAllNonBankTables() const
{
    QList<const AbstractBooksTable *> nonBankTables;
    QList<QTableView *> allViews = ui->toolBoxSalePurchases->findChildren<QTableView *>();
    for (auto view : allViews)
    {
        nonBankTables << static_cast<const AbstractBooksTable *>(view->model());
    }
    return nonBankTables;
}

AbstractBooksTableBank *PaneBookKeeping::getVisibleBankTable() const
{
    QWidget *currentWidget = ui->toolBoxBanks->currentWidget();
    if (auto *view = qobject_cast<QTableView*>(currentWidget)) {
        return qobject_cast<AbstractBooksTableBank*>(view->model());
    }
    return nullptr;
}
