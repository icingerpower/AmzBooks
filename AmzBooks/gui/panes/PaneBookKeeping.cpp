#include <QDateTime>
#include <QDebug>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include "books/AbstractBooksTableBank.h"
#include "books/EntrySelfTable.h"
#include "books/BooksConnections.h"
#include "banks/AbstractBankStatement.h"
#include "books/PurchaseInvoiceTable.h"
#include "books/PurchaseAmzPaymentsTable.h"
#include "books/JournalTable.h"
#include "books/JournalEntryFactory.h"
#include "books/ReportGenerator.h"
#include "books/BookSaverFull.h"
#include "books/BooksAccountsSalesTable.h"
#include "books/BookAccountPurchaseTable.h"
#include "books/BookAccountSelfVatTable.h"
#include <QCoroTask>

#include <QTableView>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>

#include "CurrencyRateManager.h"

#include "PaneBookKeeping.h"
#include "ui_PaneBookKeeping.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>
#include <QShowEvent>
#include <QFileInfo>
#include <QSet>

#include "../../common/utils/CsvHeader.h"
#include "ExceptionWithTitleText.h"
#include "../dialogs/DialogAddSelfEntry.h"
#include "../dialogs/DialogPurchaseInvoices.h"
#include "../dialogs/DialogAmzPayments.h"
#include "../dialogs/DialogEditPurchase.h"
#include "../dialogs/DialogEditPurchases.h"
#include "gui/dialogs/DialogEditServiceClients.h"
#include "gui/dialogs/DialogAddSaleService.h"
#include "gui/dialogs/DialogVatParams.h"
#include "books/ServiceSalesBooksTable.h"
#include "books/ServiceClientManager.h"
#include "gui/delegates/ComboBoxDelegate.h"
#include "books/VatResolver.h"
#include "orders/InvoicingInfo.h"
#include <QUrl>
#include "books/TaxResolver.h"
#include "books/CompanyInfosTable.h"
#include "books/CompanyAddressTable.h"
#include "books/BookAccountPurchaseTable.h"
#include "books/InvoiceGenerator.h"
#include "books/VatNumbersTable.h"
#include "orders/OrderManager.h"
#include "orders/Shipment.h"
#include "orders/InvoicingInfo.h"
#include "inventory/InventoryMoveTree.h"
#include "CountriesEu.h"

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

void PaneBookKeeping::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_splitterInitialized) {
        m_splitterInitialized = true;
        QTimer::singleShot(0, this, [this]() {
            int total = ui->splitterMain->height();
            ui->splitterMain->setSizes({100, total - 100});
        });
    }
}

void PaneBookKeeping::loadYearSelected()
{
    setCursor(Qt::WaitCursor);
    _setSubButtonsEnabled(true);
    _updateServiceButtonsEnabled();
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
    qDebug() << "[PaneBookKeeping] generateBookKeeping() button clicked - starting task";
    auto task = generateBookKeepingAsync();
    
    // To properly start a lazy QCoro::Task without co_awaiting in a non-coroutine:
    QCoro::connect(std::move(task), this, []() {
        qDebug() << "[PaneBookKeeping] generateBookKeepingAsync() task finished";
    });
}

QCoro::Task<> PaneBookKeeping::generateBookKeepingAsync()
{
    qDebug() << "[PaneBookKeeping] generateBookKeepingAsync() entered";
    // 1. Ask for output directory
    QSettings settings;
    QString lastDir = settings.value("lastBookKeepingDir", QDir::homePath()).toString();
    
    qDebug() << "[PaneBookKeeping] Prompting for directory. Last dir was:" << lastDir;
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select BookKeeping Folder"), lastDir);
    if (dir.isEmpty()) {
        qDebug() << "[PaneBookKeeping] Directory selection cancelled. Exiting.";
        co_return;
    }
    qDebug() << "[PaneBookKeeping] Selected directory:" << dir;
    
    settings.setValue("lastBookKeepingDir", dir);
    QDir outDir(dir);

    qDebug() << "[PaneBookKeeping] Associating tables...";
    // 2. Associate tables
    m_booksConnections->associateTablesToIds(getAllBookTables(), getSeflEntryTable());

    qDebug() << "[PaneBookKeeping] Preparing factory and dependencies...";
    QDir workingDir = WorkingDirectoryManager::instance()->workingDir();
    CompanyInfosTable companyInfo{workingDir};
    BooksAccountsSalesTable salesAccountTable(workingDir); // Load sales accounts config
    
    // Purchase Account Table needs company country code
    BookAccountPurchaseTable purchaseAccountTable(workingDir, companyInfo.getCompanyCountryCode());
    BookAccountSelfVatTable selfVatAccountTable(workingDir, companyInfo.getCompanyCountryCode());

    JournalTable journalTable(workingDir);
    const auto &apiKey = companyInfo.getApiKeyFixer();
    if (apiKey.isEmpty())
    {
        qDebug() << "[PaneBookKeeping] Fixer API key is missing. Showing warning and exiting.";
        QMessageBox::warning(
                    this,
                    tr("Fixer API key"),
                    tr("Fixer API key is needed for currency rate retrieval"));
        co_return;
    }
    qDebug() << "[PaneBookKeeping] Fetching currency rates...";
    CurrencyRateManager currencyRateManager(workingDir, apiKey);

    // Callback for adding missing accounts
    auto callbackAddIfMissing = [](const QString &title, const QString &text) -> QCoro::Task<bool> {
        DialogVatParams dialog(title, text);
        co_return dialog.exec() == QDialog::Accepted;
    };

    JournalEntryFactory factory(&currencyRateManager, &companyInfo, &salesAccountTable, &purchaseAccountTable, &journalTable, &selfVatAccountTable);

    QHash<QString, QMultiMap<QDate, QSharedPointer<JournalEntry>>> journal_date_entries;

    // Helper to add entry
    auto addEntry = [&](const QSharedPointer<JournalEntry> &entry, const QString &journalId) {
        if (entry) {
            journal_date_entries[journalId].insert(entry->getDate(), entry);
        }
    };

    int year = ui->comboBoxYear->currentText().toInt();
    qDebug() << "[PaneBookKeeping] Selected year for generation:" << year;
    QDate dateIfConflict(year, 12, 31); // Default date? Unused here.
    QDate from(year, 1, 1);
    QDate to(year, 12, 31);

    // 4. PREPARE DATA & PROGRESS
    // --------------------------

    qDebug() << "[PaneBookKeeping] Loading sales data...";
    // 4.1 Sales Data — grouped orders (aggregated by source/month/VAT context)
    auto acceptCallbackGrouped = [](const ActivitySource*, const Shipment* shipment) { return shipment->isGrouped(); };
    auto sourceMapGrouped = m_orderManager->getActivitySource_ShipmentAndRefunds(from, to, acceptCallbackGrouped);
    // 4.1b Sales Data — ungrouped orders (one entry set per shipment/refund)
    auto acceptCallbackUngrouped = [](const ActivitySource*, const Shipment* shipment) { return !shipment->isGrouped(); };
    auto sourceMapUngrouped = m_orderManager->getActivitySource_ShipmentAndRefunds(from, to, acceptCallbackUngrouped);
    
    qDebug() << "[PaneBookKeeping] Loading purchases data...";
    auto *purchaseTable = static_cast<PurchaseInvoiceTable *>(ui->tableInvoices->model());
    QList<PurchaseInformation> invoices;
    if (purchaseTable) {
        invoices = purchaseTable->manager().getInvoices(from, to);
        qDebug() << "[PaneBookKeeping] Loaded Invoices. Count:" << invoices.size();
    } else {
        qDebug() << "[PaneBookKeeping] Warning: purchaseTable is null!";
    }

    qDebug() << "[PaneBookKeeping] Loading Bank Data...";
    // 4.3 Banks Data (Count only)
    QList<AbstractBooksTableBank *> bankTables = getAllBankTables();
    int bankRowsToProcess = 0;
    for (const AbstractBooksTableBank *bankTable : std::as_const(bankTables)) {
         int rowCount = bankTable->rowCount();
         for (int i=0; i<rowCount; ++i) {
             if (bankTable->getDate(i).year() == year) {
                 bankRowsToProcess++;
             }
         }
    }

    // Calculate Total Steps
    int ungroupedShipmentCount = 0;
    for (auto it = sourceMapUngrouped.begin(); it != sourceMapUngrouped.end(); ++it)
        ungroupedShipmentCount += it.value().size();

    // Count complete months across all grouped sources
    QDate currentDate = QDate::currentDate();
    int groupedMonthCount = 0;
    for (auto it = sourceMapGrouped.cbegin(); it != sourceMapGrouped.cend(); ++it) {
        QSet<QPair<int,int>> months;
        for (auto jt = it.value().cbegin(); jt != it.value().cend(); ++jt) {
            const QDate d = jt.key().date();
            if (d.year() < currentDate.year()
                    || (d.year() == currentDate.year() && d.month() < currentDate.month())) {
                months.insert({d.year(), d.month()});
            }
        }
        groupedMonthCount += months.size();
    }

    int totalSteps = 0;
    totalSteps += groupedMonthCount;
    totalSteps += ungroupedShipmentCount;
    totalSteps += invoices.size();
    totalSteps += bankRowsToProcess;

    qDebug() << "[PaneBookKeeping] Total entries to process:" << totalSteps;

    QProgressDialog progress(tr("Generating Bookkeeping..."), tr("Cancel"), 0, totalSteps, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    int currentStep = 0;

    qDebug() << "[PaneBookKeeping] Starting generation of entries (Sales, Purchases, Banks).";
    // -------------------

    try {
        // 5.1 Sales — grouped orders: one createEntryGrouped call per complete month per source
        for (auto it = sourceMapGrouped.begin(); it != sourceMapGrouped.end(); ++it) {
            ActivitySource source = it.key();
            const auto &allShipments = it.value();
            const QString journalId = journalTable.getJournal(&source);

            // Group shipments by (year, month)
            QMap<QPair<int,int>, QMultiMap<QDateTime, QSharedPointer<Shipment>>> monthlyShipments;
            for (auto jt = allShipments.cbegin(); jt != allShipments.cend(); ++jt) {
                const QDate d = jt.key().date();
                monthlyShipments[{d.year(), d.month()}].insert(jt.key(), jt.value());
            }

            // Process only complete months
            for (auto mt = monthlyShipments.begin(); mt != monthlyShipments.end(); ++mt) {
                const int mYear  = mt.key().first;
                const int mMonth = mt.key().second;

                // Skip current or future months
                if (mYear > currentDate.year()
                        || (mYear == currentDate.year() && mMonth >= currentDate.month())) {
                    continue;
                }

                if (progress.wasCanceled()) {
                    qDebug() << "[PaneBookKeeping] Progress was canceled (Sales grouped)";
                    co_return;
                }
                progress.setValue(currentStep++);

                const QList<QSharedPointer<JournalEntry>> entries = co_await factory.createEntryGrouped(
                    &source, mt.value(), callbackAddIfMissing);
                for (const auto &entry : entries) {
                    addEntry(entry, journalId);
                }
            }
        }

        // 5.1b Sales — ungrouped orders: one entry per shipment, customer account from OrderInfo
        for (auto it = sourceMapUngrouped.begin(); it != sourceMapUngrouped.end(); ++it) {
            ActivitySource source = it.key();
            const QString journalId = journalTable.getJournal(&source);

            for (auto jt = it.value().begin(); jt != it.value().end(); ++jt) {
                if (progress.wasCanceled()) {
                    qDebug() << "[PaneBookKeeping] Progress was canceled (Sales ungrouped)";
                    co_return;
                }
                progress.setValue(currentStep++);

                QSharedPointer<Shipment> shipment = jt.value();
                // customerAccount is stored in the Shipment, loaded via the orders JOIN
                QSharedPointer<JournalEntry> entry = co_await factory.createEntry(
                    shipment, shipment->customerAccount(), callbackAddIfMissing);
                addEntry(entry, journalId);
            }
        }

        qDebug() << "[PaneBookKeeping] Sales completed. Starting purchases...";
        // 5.2 Purchases
        for (const auto &info : std::as_const(invoices)) {
            if (progress.wasCanceled()) {
                qDebug() << "[PaneBookKeeping] Progress was canceled (Purchases)";
                co_return;
            }
            progress.setValue(currentStep++);

            QSharedPointer<JournalEntry> entry = factory.createEntry(info);
            // Determine Journal ID for Purchases (usually "AC")
            QString journalId = journalTable.getJournalPurchaseInvoice().code;
            addEntry(entry, journalId);
        }

        qDebug() << "[PaneBookKeeping] Purchases completed. Starting Banks...";
        // 5.3 Banks
        for (const AbstractBooksTableBank *bankTable : std::as_const(bankTables)) {
            int rowCount = bankTable->rowCount();
            const auto &journal = journalTable.getJournal(bankTable->getBankStatement()->getId());
            
            for (int i = 0; i < rowCount; ++i) {
                
                QDate date = bankTable->getDate(i);
                if (date.year() != year) continue; // Filter by year

                if (progress.wasCanceled()) {
                    qDebug() << "[PaneBookKeeping] Progress was canceled (Banks)";
                    co_return;
                }
                progress.setValue(currentStep++);

                QString account2 = m_booksConnections->getAccount2(const_cast<AbstractBooksTableBank*>(bankTable), i);
                if (account2.isEmpty())
                {
                    account2 = "TODO"; //TODOCEDRIC
                }
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
                addEntry(entry, journal);
            }
        }

        // 5.4 Inventory moves (EU → company country intracom stock acquisitions)
        {
            const QStringList &countryCodes = CountriesEu::getAmazonPanEuCountryCodes();

            // Build per-country shipping costs from the WidgetPurchases configuration
            QHash<QString, double> country_pricePerKilo;
            for (const QString &cc : countryCodes) {
                country_pricePerKilo[cc] = ui->widgetPurchases->getShippingPrice(cc);
            }
            country_pricePerKilo[""] = ui->widgetPurchases->getShippingPrice(""); // catch-all default

            QDate currentDate = QDate::currentDate();

            for (QDate m(from.year(), from.month(), 1); m <= to; m = m.addMonths(1)) {
                if (m.year() > currentDate.year() || (m.year() == currentDate.year() && m.month() >= currentDate.month())) {
                    continue; // Skip incomplete month (current or future)
                }

                QHash<QString, QHash<QString, int>> countryCode_sku_unitImported;
                QHash<QString, QHash<QString, int>> countryCode_sku_unitExported;
                for (const QString &cc : countryCodes) {
                    const auto imported = m_orderManager->getInventoryImported(m.year(), m.month(), cc);
                    for (auto it = imported.constBegin(); it != imported.constEnd(); ++it)
                        countryCode_sku_unitImported[cc][it.key()] += it.value();
                    const auto exported = m_orderManager->getInventoryExported(m.year(), m.month(), cc);
                    for (auto it = exported.constBegin(); it != exported.constEnd(); ++it) {
                        countryCode_sku_unitExported[cc][it.key()] += it.value();
                    }
                }

                InventoryMoveTree inventoryTree(ui->widgetPurchases->getPurchaseDir(),
                                                countryCode_sku_unitImported,
                                                countryCode_sku_unitExported,
                                                country_pricePerKilo,
                                                companyInfo.getCurrency(),
                                                &currencyRateManager,
                                                workingDir,
                                                companyInfo.getCompanyCountryCode(),
                                                nullptr);
                                                
                QSharedPointer<JournalEntry> inventoryEntry
                    = factory.createEntry(&inventoryTree, companyInfo.getCompanyCountryCode());
                    
                if (inventoryEntry) {
                    QDate endOfMonth(m.year(), m.month(), m.daysInMonth());
                    inventoryEntry->setDate(endOfMonth);
                    addEntry(inventoryEntry, journalTable.getJournalVariousOperations().code);
                }
            }
        }

        progress.setValue(totalSteps);

        // Warn if any journal ended up with no entries
        if (journal_date_entries.contains(QString{})) {
            QMessageBox::warning(this, tr("Empty Journals"),
                                 tr("No bookkeeping entries were generated for year %1. "
                                    "Make sure to have the journal in settings.")
                                     .arg(year));
            co_return;
        }

        qDebug() << "[PaneBookKeeping] Generating complete. Saving...";
        // 6. Save
        BookSaverFull saver;
        saver.save(journal_date_entries, outDir);
        qDebug() << "[PaneBookKeeping] Saved successfully.";
        QMessageBox::information(this, tr("Success"), tr("Bookkeeping generated successfully in %1").arg(outDir.absolutePath()));

        // Check if there are orders without invoices in the period
        auto noInvoicesList = m_orderManager->getShipmentAndRefundsNoInvoices(from, to);
        if (noInvoicesList && !noInvoicesList->isEmpty()) {
            int count = 0;
            for (const auto &entry : *noInvoicesList) {
                for (bool needed : entry.invoicesToDo) {
                    if (needed) {
                        count++;
                    }
                }
            }
            if (count > 0) {
                auto answer = QMessageBox::question(
                    this,
                    tr("Generate Invoices"),
                    tr("%1 order(s) in %2 do not have invoices yet. Do you want to generate them now?")
                        .arg(count).arg(year),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::Yes);
                if (answer == QMessageBox::Yes) {
                    generateInvoices();
                }
            }
        }
    } catch (const ExceptionWithTitleText &e) {
        qDebug() << "[PaneBookKeeping] ExceptionWithTitleText catching in loop:" << e.errorTitle() << "-" << e.errorText();
        QMessageBox::critical(this, e.errorTitle(), e.errorText());
    } catch (const std::exception &e) {
        qDebug() << "[PaneBookKeeping] std::exception catching in loop:" << e.what();
        QMessageBox::critical(this, tr("Error"), tr("Failed to generate documents: %1").arg(e.what()));
    } catch (...) {
        qDebug() << "[PaneBookKeeping] Unknown exception catching in loop!";
        QMessageBox::critical(this, tr("Error"), tr("An unknown error occurred during generation."));
    }
}

void PaneBookKeeping::generateInvoices()
{
    // 1. Ask for output directory (persist last used path in QSettings)
    QSettings settings;
    QString lastDir = settings.value("lastInvoicesDir", QDir::homePath()).toString();
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Invoices Output Folder"), lastDir);
    if (dir.isEmpty()) return;
    settings.setValue("lastInvoicesDir", dir);
    QDir outDir(dir);

    // 2. Get the period from the UI year selector
    int year = ui->comboBoxYear->currentText().toInt();
    QDate from(year, 1, 1);
    QDate to(year, 12, 31);

    // 3. Retrieve orders without invoices for the period
    auto noInvoicesMap = m_orderManager->get_channel_site_ShipmentAndRefundsNoInvoices(from, to);
    if (!noInvoicesMap || noInvoicesMap->isEmpty()) {
        QMessageBox::information(this, tr("No Invoices to Generate"),
            tr("All orders for %1 already have invoices.").arg(year));
        return;
    }

    // 4. Count total invoices to generate (for the progress dialog)
    int totalSteps = 0;
    for (auto chanIt = noInvoicesMap->cbegin(); chanIt != noInvoicesMap->cend(); ++chanIt)
        for (auto storeIt = chanIt.value().cbegin(); storeIt != chanIt.value().cend(); ++storeIt)
            for (auto ctxIt = storeIt.value().cbegin(); ctxIt != storeIt.value().cend(); ++ctxIt)
                for (bool needed : ctxIt.value().invoicesToDo)
                    if (needed) totalSteps++;

    // 5. Set up the InvoiceGenerator with company info and currency rates
    QDir workingDir = WorkingDirectoryManager::instance()->workingDir();
    CompanyInfosTable companyInfos(workingDir);
    CompanyAddressTable companyAddress(workingDir);
    const QString apiKey = companyInfos.getApiKeyFixer();
    CurrencyRateManager currencyRates(workingDir, apiKey);
    VatNumbersTable vatNumbers(workingDir);
    InvoiceGenerator generator(workingDir, &companyInfos, &companyAddress, &currencyRates, &vatNumbers);

    // 6. Progress dialog
    QProgressDialog progress(tr("Generating Invoices..."), tr("Cancel"), 0, totalSteps, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    int currentStep = 0;
    int generated = 0;
    int errors = 0;
    QStringList errorMsgs;

    // 7. Iterate channel → store → tax context → group of shipments
    bool cancelled = false;
    for (auto chanIt = noInvoicesMap->cbegin(); chanIt != noInvoicesMap->cend() && !cancelled; ++chanIt) {
        const QString &channel = chanIt.key();
        for (auto storeIt = chanIt.value().cbegin(); storeIt != chanIt.value().cend() && !cancelled; ++storeIt) {
            const QString &store = storeIt.key();
            for (auto ctxIt = storeIt.value().cbegin(); ctxIt != storeIt.value().cend() && !cancelled; ++ctxIt) {
                if (progress.wasCanceled()) { cancelled = true; break; }

                const TaxResolver::TaxContext &taxContext = ctxIt.key();
                const OrderManager::ShipmentRefundsWithUpdates &entry = ctxIt.value();

                if (entry.shipmentsRefundsSameActivity.isEmpty()) continue;

                // Date: use first shipment's first activity date
                QDate date;
                const auto &firstShipment = entry.shipmentsRefundsSameActivity.first();
                if (firstShipment && !firstShipment->getActivities().isEmpty())
                    date = firstShipment->getActivities().first().getDateTime().date();
                if (!date.isValid()) date = from;

                // Preserve existing invoice number as base (for revisions)
                std::optional<QString> existingNumber;
                if (entry.invoicingInfo) {
                    auto optNum = entry.invoicingInfo->getInvoiceNumber();
                    if (optNum.has_value() && !optNum->isEmpty())
                        existingNumber = optNum;
                }

                // Build per-entry shipment IDs so that entries from the same order
                // share a base number while entries from different orders each get
                // their own sequential number.
                QStringList shipmentIds;
                shipmentIds.reserve(entry.shipmentsRefundsSameActivity.size());
                for (const auto &shipment : entry.shipmentsRefundsSameActivity) {
                    if (shipment && !shipment->getActivities().isEmpty())
                        shipmentIds.append(shipment->getActivities().first().getEventId());
                    else
                        shipmentIds.append(QString());
                }

                // Generate invoice numbers for all shipments/refunds in this group
                QStringList invoiceNumbers = generator.getNextInvoiceNumbers(
                    date, taxContext, channel, store, entry.invoicesToDo, existingNumber, shipmentIds);

                // Fallback address when none is recorded (e.g. manual service sales)
                const Address emptyAddr("", "", "", "", "", "", "", "", "", "", "", "");

                for (int i = 0; i < entry.shipmentsRefundsSameActivity.size() && !cancelled; ++i) {
                    if (!entry.invoicesToDo.value(i, false)) continue;
                    if (progress.wasCanceled()) { cancelled = true; break; }
                    progress.setValue(currentStep++);

                    const QString &invoiceNumber = invoiceNumbers.value(i);
                    if (invoiceNumber.isEmpty()) continue;

                    const auto &shipment = entry.shipmentsRefundsSameActivity[i];
                    if (!shipment || shipment->getActivities().isEmpty()) { errors++; errorMsgs << tr("Empty activities"); continue; }

                    const QString &orderId = shipment->getActivities().first().getEventId();
                    const QString &activityId = shipment->getActivities().first().getActivityId();
                    if (activityId.isEmpty()) {
                        errors++;
                        errorMsgs << tr("Empty shipmentId");
                        continue;
                    }

                    // Get InvoicingInfo per order (most precise), fallback to group-level info.
                    // The existing info already contains line items and payment date; we only
                    // add the invoice number — preserving all other recorded information.
                    QSharedPointer<InvoicingInfo> info = m_orderManager->getInvoicingInfo(activityId);
                    if (!info) {
                        info = entry.invoicingInfo;
                    }
                    if (!info) {
                        errors++;
                        errorMsgs << tr("Missing invoicing info for %1").arg(orderId);
                        continue;
                    }

                    const Address &addressTo = entry.addressTo ? *entry.addressTo : emptyAddr;

                    // Sanitize invoice number for use as a filename
                    QString sanitized = invoiceNumber;
                    sanitized.replace('/', '-').replace('\\', '-');
                    QString subDirName = QString("%1/%2").arg(date.year()).arg(date.month(), 2, 10, QChar('0'));
                    QDir subDir(outDir.filePath(subDirName));
                    subDir.mkpath(".");
                    const QString pdfPath = subDir.absoluteFilePath(sanitized + ".pdf");

                    // Set prevNumber only for actual revision invoices (suffix -R\d+),
                    // not simply because this is the second shipment in the group.
                    QString prevNumber;
                    {
                        const int rIdx = invoiceNumber.lastIndexOf("-R");
                        if (rIdx != -1) {
                            const QString suffix = invoiceNumber.mid(rIdx + 2);
                            bool ok;
                            suffix.toInt(&ok);
                            if (ok) prevNumber = invoiceNumber.left(rIdx);
                        }
                    }

                    try {
                        generator.generateInvoice(invoiceNumber, prevNumber, pdfPath,
                                                   addressTo, *info, orderId, *m_orderManager);
                        generated++;
                    } catch (const std::exception &ex) {
                        qWarning() << "[generateInvoices] Failed for" << orderId << ":" << ex.what();
                        errors++;
                        errorMsgs << tr("Failed for %1: %2").arg(orderId, ex.what());
                    }
                }
            }
        }
    }

    progress.setValue(totalSteps);

    QString msg = tr("Generated %1 invoice(s).").arg(generated);
    if (errors > 0) {
        msg += tr("\n%1 error(s) occurred:\n").arg(errors);
        int displayCount = qMin(10, errorMsgs.size());
        for (int i = 0; i < displayCount; ++i) {
            msg += "- " + errorMsgs[i] + "\n";
        }
        if (errorMsgs.size() > 10) {
            msg += tr("... and %1 more error(s)").arg(errorMsgs.size() - 10);
        }
    }
    QMessageBox::information(this, tr("Invoice Generation Complete"), msg);

    generateSaleReports(outDir);
}

void PaneBookKeeping::regenerateInvoices()
{
    // 1. Ask for the output folder
    QSettings settings;
    QString lastDir = settings.value("lastInvoicesDir", QDir::homePath()).toString();
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Invoices Output Folder"), lastDir);
    if (dir.isEmpty()) {
        return;
    }
    settings.setValue("lastInvoicesDir", dir);
    QDir outDir(dir);

    // 2. Build the date range from the selected year
    int year = ui->comboBoxYear->currentText().toInt();
    QDate from(year, 1, 1);
    QDate to(year, 12, 31);

    // 3. Set up InvoiceGenerator (loads the CSV registry)
    QDir workingDir = WorkingDirectoryManager::instance()->workingDir();
    CompanyInfosTable companyInfos(workingDir);
    CompanyAddressTable companyAddress(workingDir);
    const QString apiKey = companyInfos.getApiKeyFixer();
    CurrencyRateManager currencyRates(workingDir, apiKey);
    VatNumbersTable vatNumbers(workingDir);
    InvoiceGenerator generator(workingDir, &companyInfos, &companyAddress, &currencyRates, &vatNumbers);

    // 4. Ask whether to delete existing PDFs in the date range before regenerating
    auto answer = QMessageBox::question(
        this,
        tr("Delete Existing Invoices"),
        tr("Do you want to delete existing invoice PDFs for %1 in the selected folder before regenerating?\n"
           "(Only files for invoices within the %1 date range will be removed — "
           "files outside the range are left untouched.)")
            .arg(year),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (answer == QMessageBox::Yes) {
        for (int row = 0; row < generator.rowCount(); ++row) {
            QDate recDate = generator.data(generator.index(row, InvoiceGenerator::ColDate)).toDate();
            if (recDate < from || recDate > to)
                continue;
            const QString invoiceNumber =
                generator.data(generator.index(row, InvoiceGenerator::ColInvoiceNumber)).toString();
            if (invoiceNumber.isEmpty())
                continue;
            QString sanitized = invoiceNumber;
            sanitized.replace('/', '-').replace('\\', '-');
            QString subDirName = QString("%1/%2").arg(recDate.year()).arg(recDate.month(), 2, 10, QChar('0'));
            QDir subDir(outDir.filePath(subDirName));
            const QString pdfPath = subDir.absoluteFilePath(sanitized + ".pdf");
            if (QFile::exists(pdfPath))
                QFile::remove(pdfPath);
        }
    }

    // 5. Regenerate
    try {
        generator.regenerateInvoices(outDir, from, to, *m_orderManager);

        QMessageBox::information(
            this,
            tr("Regeneration Complete"),
            tr("Invoices for %1 have been regenerated in:\n%2")
                .arg(year)
                .arg(outDir.absolutePath()));
    } catch (const ExceptionWithTitleText &e) {
        QMessageBox::warning(this, e.errorTitle(), e.errorText());
    }

    generateSaleReports(outDir);
}

void PaneBookKeeping::generateSaleReports(const QDir &dirTo)
{
    const int year = ui->comboBoxYear->currentText().toInt();
    const QDate from(year, 1, 1);
    const QDate to(year, 12, 31);

    auto schemeShortStr = [](TaxScheme s) -> QString {
        switch (s) {
        case TaxScheme::DomesticVat:               return "DOM";
        case TaxScheme::EuOssUnion:                return "OSS";
        case TaxScheme::EuOssNonUnion:             return "OSS-NU";
        case TaxScheme::EuIoss:                    return "IOSS";
        case TaxScheme::ImportVat:                 return "IMP";
        case TaxScheme::ReverseChargeImport:       return "RCI";
        case TaxScheme::ReverseChargeDomestic:     return "RCD";
        case TaxScheme::MarketplaceDeemedSupplier: return "MDS";
        case TaxScheme::Exempt:                    return "EXP";
        case TaxScheme::OutOfScope:                return "HRS";
        default:                                   return "?";
        }
    };

    const QDir workingDir = WorkingDirectoryManager::instance()->workingDir();
    CompanyInfosTable companyInfos(workingDir);
    BooksAccountsSalesTable salesAccounts(workingDir);

    auto acceptGrouped = [](const ActivitySource *, const Shipment *s) {
        return s->isGrouped();
    };
    const auto sourceMapGrouped =
        m_orderManager->getActivitySource_ShipmentAndRefunds(from, to, acceptGrouped);

    const QStringList tableHeaders = {
        tr("Store"), tr("Date"), tr("Order ID"), tr("Shipment/Refund ID"),
        tr("Type"), tr("Untaxed amount"), tr("Taxes"), tr("Taxed amount"),
        tr("Currency"), tr("Orig full amount"), tr("Orig currency"),
        tr("Vat rate"), tr("Vat scheme"), tr("Country from"), tr("Country to"),
        tr("Tax number"), tr("Invoice number"), tr("Invoice")
    };
    const int NB_COLS = tableHeaders.size();

    for (int month = 1; month <= 12; ++month) {
        // Skip current and future months (data may be incomplete)
        const QDate today = QDate::currentDate();
        if (year > today.year() || (year == today.year() && month >= today.month()))
            continue;

        ReportGenerator report;
        report.setLandscape(true);
        report.setPageSize(QPageSize::A3);
        report.setFontScale(1.5);

        const QString monthStr = QString("%1").arg(month, 2, 10, QChar('0'));
        report.addTitle(tr("Sale amazon") + QString(" %1/%2").arg(year).arg(monthStr));

        bool hasData = false;

        for (auto it = sourceMapGrouped.cbegin(); it != sourceMapGrouped.cend(); ++it) {
            ActivitySource source = it.key();
            const auto &allShipments = it.value();

            // Filter to this month only
            QMultiMap<QDateTime, QSharedPointer<Shipment>> monthlyShipments;
            QList<QSharedPointer<Shipment>> monthlyShipmentList;
            for (auto jt = allShipments.cbegin(); jt != allShipments.cend(); ++jt) {
                const QDate d = jt.key().date();
                if (d.year() == year && d.month() == month) {
                    monthlyShipments.insert(jt.key(), jt.value());
                    monthlyShipmentList.append(jt.value());
                }
            }
            if (monthlyShipments.isEmpty())
                continue;

            const QHash<QString, QString> orderId_store =
                m_orderManager->getStores(monthlyShipmentList);

            const QDate firstDate = monthlyShipments.firstKey().date();
            const QDate entryDate = QDate(firstDate.year(), firstDate.month(),
                                          firstDate.daysInMonth());

            QList<JournalEntryFactory::GroupedShipmentData> vatGroups =
                JournalEntryFactory::computeGrouping(&source, monthlyShipments,
                                                     entryDate, orderId_store);

            // Resolve book accounts for each group (best-effort, no user prompt)
            for (auto &g : vatGroups) {
                const VatCountries vc = salesAccounts.resolveVatCountries(
                    g.taxScheme, companyInfos.getCompanyCountryCode(),
                    g.countryFrom, g.countryTo);
                const BooksAccountsSalesTable::Accounts accts =
                    salesAccounts.getAccountsIfPresent(vc, g.vatRatePct);
                g.saleAccount = accts.saleAccount;
                g.vatAccount  = accts.vatAccount;
            }

            for (const auto &group : std::as_const(vatGroups)) {
                hasData = true;

                // Subtitle: e.g. "Amazon Europe | DOM FR→FR 20.00% EUR [706000 / 445711]"
                QString subtitle =
                    QString("%1 %2 | %3 %4\u2192%5 %6% %7")
                        .arg(source.channel, source.subchannel,
                             schemeShortStr(group.taxScheme),
                             group.countryFrom, group.countryTo,
                             QString::number(group.vatRatePct, 'f', 2),
                             group.currency);
                if (!group.saleAccount.isEmpty()) {
                    subtitle += " [" + group.saleAccount;
                    if (group.vatRatePct > 0.01 && !group.vatAccount.isEmpty())
                        subtitle += " / " + group.vatAccount;
                    subtitle += "]";
                }
                report.addSubtitle(subtitle);

                report.startTable(tableHeaders);

                // Sort rows by store then date
                QList<JournalEntryFactory::ShipmentReportInfo> rows = group.shipments;
                std::sort(rows.begin(), rows.end(),
                    [](const JournalEntryFactory::ShipmentReportInfo &a,
                       const JournalEntryFactory::ShipmentReportInfo &b) {
                        if (a.store != b.store) return a.store < b.store;
                        return a.date < b.date;
                    });

                double sumUntaxed = 0.0, sumTaxes = 0.0, sumTaxed = 0.0;

                const QDir invoiceSubDir(dirTo.filePath(
                    QString("%1/%2").arg(year).arg(monthStr)));

                for (const auto &row : std::as_const(rows)) {
                    sumUntaxed += row.untaxedAmount;
                    sumTaxes   += row.taxes;
                    sumTaxed   += row.taxedAmount;

                    QString invoiceNumberStr;
                    QString invoiceLinkHtml;
                    {
                        const QSharedPointer<InvoicingInfo> info =
                            m_orderManager->getInvoicingInfo(row.shipmentRefundId);
                        if (info && info->getInvoiceNumber()) {
                            invoiceNumberStr = *info->getInvoiceNumber();
                        }
                        if (info && info->getInvoiceLink()) {
                            invoiceLinkHtml =
                                "<a href=\"" + *info->getInvoiceLink() + "\">" + tr("Open") + "</a>";
                        }
                    }

                    report.addRow({
                        row.store,
                        row.date.toString("yyyy-MM-dd"),
                        row.orderId,
                        row.shipmentRefundId,
                        row.isRefund ? tr("Refund") : tr("Shipment"),
                        QString::number(row.untaxedAmount, 'f', 2),
                        QString::number(row.taxes,         'f', 2),
                        QString::number(row.taxedAmount,   'f', 2),
                        row.currency,
                        QString::number(row.origTaxedAmount, 'f', 2),
                        row.origCurrency,
                        QString::number(row.vatRatePct, 'f', 2) + "%",
                        schemeShortStr(row.taxScheme),
                        row.countryFrom,
                        row.countryTo,
                        row.isCompany ? row.taxNumber : QString{},
                        invoiceNumberStr,
                        invoiceLinkHtml
                    });
                }

                // Total row
                QStringList totalRow(NB_COLS, QString{});
                totalRow[0]  = tr("Total");
                totalRow[5]  = QString::number(sumUntaxed, 'f', 2);
                totalRow[6]  = QString::number(sumTaxes,   'f', 2);
                totalRow[7]  = QString::number(sumTaxed,   'f', 2);
                totalRow[8]  = group.currency;
                report.addRowTotal(totalRow);

                report.endTable();
            }
        }

        if (hasData) {
            const QString subDirName = QString("%1/%2").arg(year).arg(monthStr);
            QDir subDir(dirTo.filePath(subDirName));
            subDir.mkpath(".");
            const QString reportPath = subDir.absoluteFilePath(
                QString("sale_amazon_%1_%2.pdf").arg(year).arg(monthStr));
            try {
                report.save(reportPath);
            } catch (const std::exception &ex) {
                qWarning() << "[generateSaleReports] Failed to save report:" << ex.what();
            }
        }
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
            //QMessageBox::information(this, tr("Association Successful"),
                //tr("Successfully associated %1 bank row(s) with the selected self-entry.")
                //.arg(bankSelection.size()));
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
    for (const AbstractBooksTable *bookTable : std::as_const(bookTables)) {
        const QList<QTableView *> &allViews = ui->toolBoxSalePurchases->findChildren<QTableView *>();
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
        for (const QModelIndex &index : std::as_const(selfSelection)) {
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
        for (const QModelIndex &index : std::as_const(selfSelection)) {
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
    for (const AbstractBooksTable *bookTable : std::as_const(bookTables)) {
        QList<QTableView *> allViews = ui->toolBoxSalePurchases->findChildren<QTableView *>();
        for (QTableView *view : std::as_const(allViews)) {
            if (view->model() == bookTable) {
                view->viewport()->update();
                break;
            }
        }
    }
    //QMessageBox::information(this, tr("Dissociation Successful"),
                             //tr("Successfully dissociated %1 row(s).").arg(disconnectedCount));
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
        
        const CompanyInfosTable companyInfos{WorkingDirectoryManager::instance()->workingDir()};
        const QString companyCurrency = companyInfos.getCurrency();
        
        const BookAccountPurchaseTable purchaseAccountTable{WorkingDirectoryManager::instance()->workingDir(), companyInfos.getCompanyCountryCode()};
        
        PurchaseInformation info = PurchaseInvoiceManager::decode(fi.fileName(), &purchaseAccountTable);

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

            if (info.countryCodeFrom.isEmpty() && info.countryCodeTo.isEmpty()) {
                if (purchaseTable->manager().isSupplierWithCountries(info.accountSupplier)) {
                    if (QMessageBox::question(
                            this,
                            tr("Missing Country"),
                            tr("The supplier '%1' usually has country information, but it is missing here. "
                               "Are you sure you want to proceed?")
                                .arg(info.accountSupplier),
                            QMessageBox::Yes | QMessageBox::No,
                            QMessageBox::No) != QMessageBox::Yes) {
                        continue;
                    }
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
    
    const BookAccountPurchaseTable purchaseAccountTable{WorkingDirectoryManager::instance()->workingDir(), companyInfos.getCompanyCountryCode()};

    DialogEditPurchases dialog(&purchaseAccountTable, fileNames, companyCurrency, this);
    if (dialog.exec() == QDialog::Accepted) {
        auto purchaseTable = static_cast<PurchaseInvoiceTable *>(ui->tableInvoices->model());
        QList<PurchaseInformation> invoicesToAdd = dialog.getInfos();

        // Loop 1: validate — ask about missing countries without adding anything yet
        bool aborted = false;
        QSet<QString> confirmedSuppliers; // suppliers already confirmed once
        for (const PurchaseInformation &info : std::as_const(invoicesToAdd)) {
            if (info.countryCodeFrom.isEmpty() && info.countryCodeTo.isEmpty()) {
                if (!confirmedSuppliers.contains(info.accountSupplier)
                        && purchaseTable->manager().isSupplierWithCountries(info.accountSupplier)) {
                    if (QMessageBox::question(
                            this,
                            tr("Missing Country"),
                            tr("For '%1', country information is missing though the supplier usually has it. "
                               "Are you sure you want to add it?")
                                .arg(info.accountSupplier),
                            QMessageBox::Yes | QMessageBox::No,
                            QMessageBox::No) != QMessageBox::Yes) {
                        aborted = true;
                        break;
                    }
                    confirmedSuppliers.insert(info.accountSupplier);
                }
            }
        }

        if (aborted) {
            QMessageBox::information(this, tr("Import Cancelled"),
                                     tr("Import cancelled. No invoices were added."));
            return;
        }

        // Loop 2: add all invoices (validation passed)
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
    for (const QModelIndex &idx : std::as_const(selection)) {
        rowIds << purchaseTable->getRowId(idx);
    }

    for (const QString &id : std::as_const(rowIds)) {
        if (m_booksConnections->contains(purchaseTable->getId(), id)) {
            // Find the index for the current id to disconnect
            for (const QModelIndex &idx : std::as_const(selection)) {
                if (purchaseTable->getRowId(idx) == id) {
                    m_booksConnections->disconnect(purchaseTable, idx);
                    break;
                }
            }
        }
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
    VatResolver vatResolver(WorkingDirectoryManager::instance()->workingDir());
    TaxResolver taxResolver(WorkingDirectoryManager::instance()->workingDir());

    auto onMissingVatRate = [&vatResolver, this]() -> bool {
        DialogVatParams dlg(
            tr("Missing VAT Rate"),
            tr("No VAT rate was found for this service sale. Please configure it in the VAT settings."),
            this
        );
        if (dlg.exec() == QDialog::Accepted) {
            vatResolver.reload();
            return true;
        }
        return false;
    };

    DialogAddSaleService dialog(&clientManager, this);
    if (dialog.exec() == QDialog::Accepted) {
        try {
            serviceTable->createSale(
                &clientManager,
                dialog.getSelectedClientRow(),
                dialog.getDate(),
                dialog.getUnitPrice() * dialog.getQuantity(),
                dialog.getCurrency(),
                dialog.getInvoiceId(),
                dialog.getServiceTitle(),
                dialog.getQuantity(),
                dialog.getAccount(),
                vatResolver,
                taxResolver,
                dialog.getPaymentType(),
                dialog.getPaymentDays(),
                dialog.getVatOnPayment(),
                onMissingVatRate
            );
        } catch (const ExceptionWithTitleText &e) {
            QMessageBox::warning(this, e.errorTitle(), e.errorText());
        } catch (const std::exception &e) {
            QMessageBox::warning(this, tr("Error"), tr("An error occurred: %1").arg(e.what()));
        }
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

    // Provide an InvoiceGenerator so that remove() can clean up the CSV registry
    // for any sale that already had an invoice generated.
    QDir workingDir{WorkingDirectoryManager::instance()->workingDir()};
    CompanyInfosTable companyInfos(workingDir);
    CompanyAddressTable companyAddress(workingDir);
    const QString apiKey = companyInfos.getApiKeyFixer();
    CurrencyRateManager currencyRates(workingDir, apiKey);
    VatNumbersTable vatNumbers(workingDir);
    InvoiceGenerator generator(workingDir, &companyInfos, &companyAddress, &currencyRates, &vatNumbers);
    serviceTable->setInvoiceGenerator(&generator);

    for(const QString &id : ids) {
        serviceTable->remove(id);
    }

    serviceTable->setInvoiceGenerator(nullptr);
}

void PaneBookKeeping::serviceEditClients()
{
    ServiceClientManager clientManager(WorkingDirectoryManager::instance()->workingDir());
    DialogEditServiceClients dialog(&clientManager, this);
    dialog.exec();
    // Clients saved automatically by manager on change/destruct
    _updateServiceButtonsEnabled();
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
    VatResolver vatResolver(WorkingDirectoryManager::instance()->workingDir());
    TaxResolver taxResolver(WorkingDirectoryManager::instance()->workingDir());

    auto onMissingVatRate = [&vatResolver, this]() -> bool {
        DialogVatParams dlg(
            tr("Missing VAT Rate"),
            tr("No VAT rate was found for this service sale. Please configure it in the VAT settings."),
            this
        );
        if (dlg.exec() == QDialog::Accepted) {
            vatResolver.reload();
            return true;
        }
        return false;
    };

    DialogAddSaleService dialog(&clientManager, this);

    dialog.setUnitPrice(qAbs(totalAmount));
    dialog.setReference(labels.join(" + "));
    dialog.setDate(date);

    if (dialog.exec() == QDialog::Accepted) {
        auto *serviceTable = static_cast<ServiceSalesBooksTable *>(ui->tableServices->model());
        if (serviceTable) {
            serviceTable->createSale(
                &clientManager,
                dialog.getSelectedClientRow(),
                dialog.getDate(),
                dialog.getUnitPrice() * dialog.getQuantity(),
                dialog.getCurrency(),
                dialog.getInvoiceId(),
                dialog.getServiceTitle(),
                dialog.getQuantity(),
                dialog.getAccount(),
                vatResolver,
                taxResolver,
                dialog.getPaymentType(),
                dialog.getPaymentDays(),
                dialog.getVatOnPayment(),
                onMissingVatRate
            );
        }
    }
}

void PaneBookKeeping::amzPaymentAdd()
{
    QSettings settings;
    QString lastDir = settings.value("lastAmzPaymentDir", QDir::homePath()).toString();

    QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("Select Amazon Payment File"),
        lastDir,
        tr("Payment Files (*.pdf *.png *.jpg *.jpeg);;All Files (*)"));

    if (fileName.isEmpty())
        return;

    settings.setValue("lastAmzPaymentDir", QFileInfo(fileName).absolutePath());

    auto amzTable = getAmzPaymentsTable();

    try {
        AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(fileName);

        if (!info.hasBalanceStart && !info.hasBalanceEnd) {
            if (QMessageBox::warning(
                    this,
                    tr("Missing Balance Info"),
                    tr("The file '%1' has no balance-begin or balance-end tokens.\n"
                       "Both balances will be treated as 0.00.\n\n"
                       "Do you want to continue?")
                        .arg(QFileInfo(fileName).fileName()),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No) != QMessageBox::Yes) {
                return;
            }
        }
        amzTable->add(fileName, info);

    } catch (const ExceptionWithTitleText &e) {
        QMessageBox::warning(this, e.errorTitle(), e.errorText());
    } catch (...) {
        QMessageBox::warning(this, tr("Error"), tr("Unknown error adding payment."));
    }
}

void PaneBookKeeping::amzPaymentAddMany()
{
    QSettings settings;
    QString lastDir = settings.value("lastAmzPaymentDir", QDir::homePath()).toString();

    QStringList fileNames = QFileDialog::getOpenFileNames(
        this,
        tr("Select Amazon Payment Files"),
        lastDir,
        tr("Payment Files (*.pdf *.png *.jpg *.jpeg);;All Files (*)"));

    if (fileNames.isEmpty())
        return;

    settings.setValue("lastAmzPaymentDir", QFileInfo(fileNames.first()).absolutePath());

    DialogAmzPayments dialog(fileNames, this);
    if (dialog.exec() == QDialog::Accepted) {
        auto amzTable = getAmzPaymentsTable();
        QList<AmzPaymentInfo> paymentsToAdd = dialog.selectedPayments();

        /*
        // Collect entries where both balance tokens are absent and ask for confirmation
        QList<AmzPaymentInfo> missingBalances;
        for (const AmzPaymentInfo &info : std::as_const(paymentsToAdd)) {
            if (!info.hasBalanceStart && !info.hasBalanceEnd)
                missingBalances.append(info);
        }

        if (!missingBalances.isEmpty()) {
            QDialog warnDialog(this);
            warnDialog.setWindowTitle(tr("Missing Balance Info Warning"));
            auto *layout = new QVBoxLayout(&warnDialog);

            auto *label = new QLabel(
                tr("The following payment(s) have no balance-begin or balance-end tokens.\n"
                   "Both balances will be treated as 0.00 for each.\n\n"
                   "Do you want to continue?"),
                &warnDialog);
            label->setWordWrap(true);
            layout->addWidget(label);

            auto *table = new QTableWidget(missingBalances.size(), 3, &warnDialog);
            table->setHorizontalHeaderLabels({tr("File"), tr("Country"), tr("Date From")});
            table->horizontalHeader()->setStretchLastSection(true);
            table->setEditTriggers(QAbstractItemView::NoEditTriggers);
            table->setSelectionMode(QAbstractItemView::NoSelection);
            table->setAlternatingRowColors(true);

            for (int i = 0; i < missingBalances.size(); ++i) {
                const AmzPaymentInfo &w = missingBalances[i];
                table->setItem(i, 0, new QTableWidgetItem(QFileInfo(w.filePath).fileName()));
                table->setItem(i, 1, new QTableWidgetItem(w.countryCode));
                table->setItem(i, 2, new QTableWidgetItem(w.dateFrom.toString(Qt::ISODate)));
            }
            table->resizeColumnsToContents();
            layout->addWidget(table);

            auto *buttons = new QDialogButtonBox(
                QDialogButtonBox::Yes | QDialogButtonBox::No, &warnDialog);
            connect(buttons, &QDialogButtonBox::accepted, &warnDialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &warnDialog, &QDialog::reject);
            layout->addWidget(buttons);

            if (warnDialog.exec() != QDialog::Accepted)
                return;
        }
//*/

        int count = 0;
        int errCount = 0;
        for (const AmzPaymentInfo &info : std::as_const(paymentsToAdd)) {
            try {
                amzTable->add(info.filePath, info);
                count++;
            } catch (...) {
                errCount++;
            }
        }

        QString msg = tr("Added %1 payment(s).").arg(count);
        if (errCount > 0)
            msg += tr("\n%1 error(s) occurred.").arg(errCount);
        QMessageBox::information(this, tr("Import Result"), msg);
    }
}

void PaneBookKeeping::amzPaymentRemove()
{
    auto amzTable = getAmzPaymentsTable();
    QModelIndexList selection = ui->tableAmzPayments->selectionModel()->selectedRows();

    if (selection.isEmpty()) {
        QMessageBox::warning(this, tr("No Selection"), tr("Please select payment(s) to remove."));
        return;
    }

    if (QMessageBox::question(
            this,
            tr("Confirm Removal"),
            tr("Are you sure you want to remove %1 payment(s)? "
               "This will delete the files from disk.")
                .arg(selection.size()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    QStringList rowIds;
    for (const QModelIndex &idx : std::as_const(selection))
        rowIds << amzTable->getRowId(idx);

    for (const QString &id : std::as_const(rowIds)) {
        if (m_booksConnections->contains(amzTable->getId(), id)) {
            for (const QModelIndex &idx : std::as_const(selection)) {
                if (amzTable->getRowId(idx) == id) {
                    m_booksConnections->disconnect(amzTable, idx);
                    break;
                }
            }
        }
        amzTable->removePayment(id);
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
    ui->tableServices->setItemDelegateForColumn(
        ServiceSalesBooksTable::IND_PAYMENT_TERM,
        new ComboBoxDelegate(ServiceClientManager::paymentTypeLabels(), ui->tableServices));

    // Amazon Payments
    auto amzPaymentsTable = new PurchaseAmzPaymentsTable(m_booksConnections, workingDir, ui->tableAmzPayments);
    ui->tableAmzPayments->setModel(amzPaymentsTable);
    ui->tableAmzPayments->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableAmzPayments->setSelectionMode(QAbstractItemView::ExtendedSelection);
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
    connect(ui->buttonGenerateInvoices,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::generateInvoices);
    connect(ui->buttonRegenerateInvoices,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::regenerateInvoices);
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

    connect(ui->buttonServiceAdd,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::serviceAddSale);
    connect(ui->buttonServiceRemove,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::serviceRemoveSale);
    connect(ui->buttonServiceCreateSel,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::serviceCreateFromSelection);
    connect(ui->buttonServiceEditCustomer,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::serviceEditClients);
    connect(ui->buttonAmzPaymentAdd,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::amzPaymentAdd);
    connect(ui->buttonAmzPaymentAddMany,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::amzPaymentAddMany);
    connect(ui->buttonAmzPaymentRemove,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::amzPaymentRemove);
}

void PaneBookKeeping::_updateServiceButtonsEnabled()
{
    ServiceClientManager clientManager(WorkingDirectoryManager::instance()->workingDir());
    bool hasClients = clientManager.rowCount() > 0;
    ui->buttonServiceAdd->setEnabled(hasClients);
    ui->buttonServiceRemove->setEnabled(hasClients);
    ui->buttonServiceCreateSel->setEnabled(hasClients);
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

PurchaseAmzPaymentsTable *PaneBookKeeping::getAmzPaymentsTable() const
{
    return static_cast<PurchaseAmzPaymentsTable *>(ui->tableAmzPayments->model());
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
