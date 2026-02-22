#include <QDateTime>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include "books/AbstractBooksTableBank.h"
#include "books/EntrySelfTable.h"
#include "books/BooksConnections.h"
#include "banks/AbstractBankStatement.h"
#include "books/PurchaseInvoiceTable.h"
#include "books/JournalTable.h"
#include "books/JournalEntryFactory.h"
#include "books/BookSaverFull.h"
#include "books/BooksAccountsSalesTable.h"
#include "books/BookAccountPurchaseTable.h"
#include <QCoroTask>

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
#include "ExceptionWithTitleText.h"
#include "../dialogs/DialogAddSelfEntry.h"
#include "../dialogs/DialogPurchaseInvoices.h"
#include "../dialogs/DialogEditPurchase.h"
#include "../dialogs/DialogEditPurchases.h"
#include "gui/dialogs/DialogEditServiceClients.h"
#include "gui/dialogs/DialogAddSaleService.h"
#include "books/ServiceSalesBooksTable.h"
#include "books/ServiceClientManager.h"
#include "books/CompanyInfosTable.h"
#include "orders/OrderManager.h"

// For confirmation message box
#include <QMessageBox>
#include <QProgressDialog>

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
    setCursor(Qt::WaitCursor);
    _setSubButtonsEnabled(true);
    bool yearOk = false;
    int year = ui->comboBoxYear->currentText().toInt(&yearOk);
    Q_ASSERT(yearOk);
    auto allBooksTables = getAllBookTables();
    for (auto &booksTable : allBooksTables)
    {
        booksTable->load(year);
    }
    setCursor(Qt::ArrowCursor);
}

void PaneBookKeeping::generateBookKeeping()
{
    auto task = generateBookKeepingAsync();
    // We start the task. Since it interacts with UI (message boxes), it should be fine running on main thread.
    // However, QCoro::Task is lazy. We need to await it or start it.
    // Common pattern for void slot:
    // (void) task; // If task constructor starts it? No check QCoro.
    // We need to ensure it runs.
    // If we can't await, we can connect it?
    // Using a lambda wrapper to launch:
    [](QCoro::Task<> t) -> QCoro::Task<> {
        co_await t;
    }(std::move(task));
}


QCoro::Task<> PaneBookKeeping::generateBookKeepingAsync()
{
    // 1. Ask for output directory
    QSettings settings;
    QString lastDir = settings.value("lastBookKeepingDir", QDir::homePath()).toString();
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select BookKeeping Folder"), lastDir);
    if (dir.isEmpty()) {
        co_return;
    }
    settings.setValue("lastBookKeepingDir", dir);
    QDir outDir(dir);

    // 2. Associate tables
    m_booksConnections->associateTablesToIds(getAllBookTables(), getSeflEntryTable());

    // 3. Prepare Factory and Dependencies
    QDir workingDir = WorkingDirectoryManager::instance()->workingDir();
    CompanyInfosTable companyInfo{workingDir};
    BooksAccountsSalesTable salesAccountTable(workingDir); // Load sales accounts config
    
    // Purchase Account Table needs company country code
    BookAccountPurchaseTable purchaseAccountTable(workingDir, companyInfo.getCompanyCountryCode());
    
    JournalTable journalTable(workingDir);
    const auto &apiKey = companyInfo.getApiKeyFixer();
    if (apiKey.isEmpty())
    {
        QMessageBox::warning(
                    this,
                    tr("Fixer API key"),
                    tr("Fixer API key is needed for currency rate retrieval"));
        co_return;
    }
    CurrencyRateManager currencyRateManager(workingDir, apiKey);

    // Callback for adding missing accounts
    auto callbackAddIfMissing = [](const QString &title, const QString &text) -> QCoro::Task<bool> {
        auto result = QMessageBox::warning(nullptr, title, text, QMessageBox::Yes | QMessageBox::No);
        co_return result == QMessageBox::Yes;
    };

    JournalEntryFactory factory(&currencyRateManager, &companyInfo, &salesAccountTable, &purchaseAccountTable, &journalTable);

    QHash<QString, QMultiMap<QDate, QSharedPointer<JournalEntry>>> journal_date_entries;

    // Helper to add entry
    auto addEntry = [&](const QSharedPointer<JournalEntry> &entry, const QString &journalId) {
        if (entry) {
            journal_date_entries[journalId].insert(entry->getDate(), entry);
        }
    };

    int year = ui->comboBoxYear->currentText().toInt();
    QDate dateIfConflict(year, 12, 31); // Default date? Unused here.
    QDate from(year, 1, 1);
    QDate to(year, 12, 31);

    // 4. PREPARE DATA & PROGRESS
    // --------------------------

    // 4.1 Sales Data
    auto acceptCallback = [](const ActivitySource*, const Shipment*) { return true; };
    auto sourceMap = m_orderManager->getActivitySource_ShipmentAndRefunds(from, to, acceptCallback);
    
    // 4.2 Purchases Data
    auto *purchaseTable = static_cast<PurchaseInvoiceTable *>(ui->tableInvoices->model());
    QList<PurchaseInformation> invoices;
    if (purchaseTable) {
        invoices = purchaseTable->manager().getInvoices(from, to);
    }

    // 4.3 Banks Data (Count only)
    QList<AbstractBooksTableBank *> bankTables = getAllBankTables();
    int bankRowsToProcess = 0;
    for (const AbstractBooksTableBank *bankTable : bankTables) {
         int rowCount = bankTable->rowCount();
         for (int i=0; i<rowCount; ++i) {
             if (bankTable->getDate(i).year() == year) {
                 bankRowsToProcess++;
             }
         }
    }

    // Calculate Total Steps
    int totalSteps = 0;
    // Sales: 1 step per source group? Or per shipment? 
    // createEntry takes a list of shipments. So 1 step per source.
    totalSteps += sourceMap.size(); 
    totalSteps += invoices.size();
    totalSteps += bankRowsToProcess;

    QProgressDialog progress(tr("Generating Bookkeeping..."), tr("Cancel"), 0, totalSteps, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    int currentStep = 0;

    // 5. GENERATE ENTRIES
    // -------------------

    // 5.1 Sales
    for (auto it = sourceMap.begin(); it != sourceMap.end(); ++it) {
        if (progress.wasCanceled()) {
            co_return;
        }
        progress.setValue(currentStep++);

        ActivitySource source = it.key();
        const auto &shipments = it.value();
        
        QSharedPointer<JournalEntry> entry = co_await factory.createEntry(&source, shipments, callbackAddIfMissing);
        QString journalId = journalTable.getJournal(&source);
        addEntry(entry, journalId);
    }

    // 5.2 Purchases
    for (const auto &info : invoices) {
         if (progress.wasCanceled()) {
             co_return;
         }
         progress.setValue(currentStep++);

         QSharedPointer<JournalEntry> entry = factory.createEntry(info);
         // Determine Journal ID for Purchases (usually "AC")
         QString journalId = journalTable.getJournalPurchaseInvoice().code;
         addEntry(entry, journalId);
    }

    // 5.3 Banks
    for (const AbstractBooksTableBank *bankTable : bankTables) {
        int rowCount = bankTable->rowCount();
        QString journalId = bankTable->getBankStatement() ? bankTable->getBankStatement()->defaultJournal() : "BQ"; // Default to BQ
        
        for (int i = 0; i < rowCount; ++i) {
            
            QDate date = bankTable->getDate(i);
            if (date.year() != year) continue; // Filter by year

            if (progress.wasCanceled()) {
                co_return;
            }
            progress.setValue(currentStep++);

            QString account2 = m_booksConnections->getAccount2(const_cast<AbstractBooksTableBank*>(bankTable), i);
            if (account2.isEmpty())
            {
                QMessageBox::warning(
                            this,
                            tr("Bank connection missing"),
                            tr("The bank connection is not done for the bank %1 on date %2").arg(
                                bankTable->getBankStatement()->getName(),
                                date.toString("yyyy/MM/dd")));
                co_return;
            }

            QSharedPointer<JournalEntry> entry = factory.createEntry(bankTable, account2, i);
            addEntry(entry, journalId);
        }
    }
    
    progress.setValue(totalSteps);

    // 6. Save
    try {
        BookSaverFull saver;
        saver.save(journal_date_entries, outDir);
        QMessageBox::information(this, tr("Success"), tr("Bookkeeping generated successfully in %1").arg(outDir.absolutePath()));
    } catch (const std::exception &e) {
        QMessageBox::critical(this, tr("Error"), tr("Failed to save documents: %1").arg(e.what()));
    }
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
        EntrySelfTable *selfTable = getSeflEntryTable();
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
        QList<AbstractBooksTable *> bookTables = getAllNonBankTables();
        
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
    EntrySelfTable *selfTable = getSeflEntryTable();
    QModelIndexList selfSelection;
    if (selfTable) {
        selfSelection = ui->tableSelfEntry->selectionModel()->selectedRows();
    }
    
    // Get selection from book tables (purchases, services, etc.)
    QList<AbstractBooksTable *> bookTables = getAllNonBankTables();
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

void PaneBookKeeping::selfEntryAdd()
{
    DialogAddSelfEntry dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        auto *model = getSeflEntryTable();
        model->addRow({dialog.getName(), dialog.getAccount()});
    }
}

void PaneBookKeeping::selfEntryRemove()
{
    const auto &selIndexes = ui->tableSelfEntry->selectionModel()->selectedIndexes();
    if (selIndexes.size() == 0) {
        QMessageBox::warning(this, tr("No Selection"), tr("Please select a self entry to remove."));
        return;
    }

    auto *model = getSeflEntryTable();
    model->remove(selIndexes.first());
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

        const CompanyInfosTable companyInfos{WorkingDirectoryManager::instance()->workingDir()};
        const QString companyCurrency = companyInfos.getCurrency();

        while (true) {
            DialogEditPurchase editDialog(info, companyCurrency, this);
            if (editDialog.exec() != QDialog::Accepted) {
                return;
            }
            info = editDialog.getInfo();

            const QDate twoMonthsAgo = QDate::currentDate().addMonths(-2);
            if (info.date < twoMonthsAgo) {
                const auto answer = QMessageBox::question(
                    this,
                    tr("Old Invoice Date"),
                    tr("The invoice date (%1) is more than 2 months old. "
                       "Are you sure this date is correct?")
                        .arg(info.date.toString(Qt::ISODate)),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No);
                if (answer != QMessageBox::Yes) {
                    continue;
                }
            }
            break;
        }

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

    } catch (const ExceptionWithTitleText &e) {
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

    const CompanyInfosTable companyInfos{WorkingDirectoryManager::instance()->workingDir()};
    const QString companyCurrency = companyInfos.getCurrency();

    DialogEditPurchases dialog(fileNames, companyCurrency, this);
    if (dialog.exec() == QDialog::Accepted) {
        auto purchaseTable = static_cast<PurchaseInvoiceTable *>(ui->tableInvoices->model());
        QList<PurchaseInformation> invoicesToAdd = dialog.getInfos();

        int count = 0;
        int errCount = 0;

        for (PurchaseInformation info : invoicesToAdd) {
            try {
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
    } catch (const ExceptionWithTitleText &e) {
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

    // Self entries
    auto selfEntriesTable = new EntrySelfTable(workingDir, ui->tableSelfEntry);
    ui->tableSelfEntry->setModel(selfEntriesTable);
    ui->tableSelfEntry->horizontalHeader()->resizeSection(0, 250);

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

    connect(ui->buttonSelfEntryAdd,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::selfEntryAdd);
    connect(ui->buttonSelfEntryRemove,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::selfEntryRemove);

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

    for (QPushButton *button : allButtons) {
        if (button != ui->buttonLoadYear
                && button != ui->buttonSelfEntryAdd
                && button != ui->buttonSelfEntryRemove)
        {
            // It is NOT in the main list, so we enable/disable it
            button->setEnabled(enabled);
        }
    }
    ui->splitter->setEnabled(enabled);
}

EntrySelfTable *PaneBookKeeping::getSeflEntryTable() const
{
    return static_cast<EntrySelfTable *>(ui->tableSelfEntry->model());
}

PurchaseInvoiceTable *PaneBookKeeping::getPurchaseInvoiceTable() const
{
    return static_cast<PurchaseInvoiceTable *>(ui->tableInvoices->model());
}

QList<AbstractBooksTable *> PaneBookKeeping::getAllBookTables() const
{
    auto allBookTables = getAllNonBankTables();
    auto allBankTables = getAllBankTables();
    for (auto bankTable : allBankTables)
    {
        allBookTables << bankTable;
    }
    return allBookTables;
}

QList<AbstractBooksTableBank *> PaneBookKeeping::getAllBankTables() const
{
    QList<AbstractBooksTableBank *> bankTables;
    QList<QTableView *> allViews = ui->toolBoxBanks->findChildren<QTableView *>();
    for (auto view : allViews)
    {
        bankTables << static_cast<AbstractBooksTableBank *>(view->model());
    }
    return bankTables;
}

QList<AbstractBooksTable *> PaneBookKeeping::getAllNonBankTables() const
{
    QList<AbstractBooksTable *> nonBankTables;
    QList<QTableView *> allViews = ui->toolBoxSalePurchases->findChildren<QTableView *>();
    for (auto view : allViews)
    {
        auto nonBankTalbe = dynamic_cast<AbstractBooksTable *>(view->model());
        if (nonBankTalbe != nullptr)
        {
            nonBankTables << nonBankTalbe;
        }
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
