#include <QDateTime>
#include <QDebug>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include "books/AbstractBooksTableBank.h"
#include "books/EntrySelfTable.h"
#include "books/BooksConnections.h"
#include "banks/AbstractBankStatement.h"
#include "books/PurchaseInvoiceTable.h"
#include "books/PurchaseInvoiceManager.h"
#include "books/PurchaseAmzPaymentsTable.h"
#include "books/PurchaseControlTable.h"
#include "books/SaleControlTable.h"
#include "books/JournalTable.h"
#include "books/JournalEntryFactory.h"
#include "books/ReportGenerator.h"
#include "books/BookSaverFull.h"
#include "books/BooksAccountsSalesTable.h"
#include "books/BookAccountsGroupedSalesTable.h"
#include "books/BookAccountPurchaseTable.h"
#include "books/BookAccountSelfVatTable.h"
#include "books/AmzPaymentSettings.h"
#include "books/BookAccountAmzBalanceTable.h"
#include <QCoroTask>

#include <QTableView>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>

#include "CurrencyRateManager.h"

#include "PaneBookKeeping.h"
#include "ui_PaneBookKeeping.h"

#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSettings>
#include <QTimer>
#include <QShowEvent>
#include <QFileInfo>
#include <QSet>

#include "../../common/utils/CsvHeader.h"
#include "ExceptionWithTitleText.h"
#include "../dialogs/DialogAddSelfEntry.h"
#include "../dialogs/DialogViewShipments.h"
#include "../dialogs/DialogPurchaseInvoices.h"
#include "../dialogs/DialogAmzPayments.h"
#include "../dialogs/DialogEditPurchase.h"
#include "../dialogs/DialogEditPurchases.h"
#include "gui/dialogs/DialogEditServiceClients.h"
#include "gui/dialogs/DialogAddSaleService.h"
#include "gui/dialogs/DialogVatParams.h"
#include "gui/dialogs/DialogPickShipment.h"
#include "books/ServiceSalesBooksTable.h"
#include "books/UngroupedOrderTable.h"
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
    //_createBooksTables();
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
    _deleteBooksTables();
    _createBooksTables();
    _setSubButtonsEnabled(true);
    _updateServiceButtonsEnabled();
    bool yearOk = false;
    int year = ui->comboBoxYear->currentText().toInt(&yearOk);
    Q_ASSERT(yearOk);
    auto allBooksTables = getAllBookTables();
    for (auto &booksTable : allBooksTables) {
        booksTable->load(year);
    }
    auto nonBankBooksTables = getAllNonBankTables();
    for (auto &booksTable : nonBankBooksTables) {
        booksTable->load(year - 1);
    }
    for (auto &booksTable : allBooksTables) {
        booksTable->sortByDate();
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

void PaneBookKeeping::displayPurchaseMissingWarning()
{
    const QDir workingDir = WorkingDirectoryManager::instance()->workingDir();

    const PurchaseControlTable controlTable{workingDir};
    if (controlTable.rowCount() == 0) {
        return;
    }

    // Last complete month: first day of previous month → last day of previous month.
    const QDate today = QDate::currentDate();
    const QDate lastMonthFirst = QDate(today.year(), today.month(), 1).addMonths(-1);
    const QDate lastMonthLast  = lastMonthFirst.addMonths(1).addDays(-1);

    // Stable month codes in calendar order (index 0 = January).
    static const QStringList MONTH_CODES = {
        PurchaseControlTable::FREQ_JAN, PurchaseControlTable::FREQ_FEB,
        PurchaseControlTable::FREQ_MAR, PurchaseControlTable::FREQ_APR,
        PurchaseControlTable::FREQ_MAY, PurchaseControlTable::FREQ_JUN,
        PurchaseControlTable::FREQ_JUL, PurchaseControlTable::FREQ_AUG,
        PurchaseControlTable::FREQ_SEP, PurchaseControlTable::FREQ_OCT,
        PurchaseControlTable::FREQ_NOV, PurchaseControlTable::FREQ_DEC
    };
    const QString lastMonthCode = MONTH_CODES.value(lastMonthFirst.month() - 1);

    const CompanyInfosTable companyInfos{workingDir};
    PurchaseInvoiceManager invoiceManager{workingDir, companyInfos.getCompanyCountryCode()};
    const QList<PurchaseInformation> invoices = invoiceManager.getInvoices(lastMonthFirst, lastMonthLast);

    QStringList missingLines;

    for (int row = 0; row < controlTable.rowCount(); ++row) {
        const QString supplierAccount = controlTable.data(controlTable.index(row, 0), Qt::EditRole).toString();
        const QString labelFilter     = controlTable.data(controlTable.index(row, 1), Qt::EditRole).toString();
        const QString frequencyCode   = controlTable.data(controlTable.index(row, 2), Qt::EditRole).toString();

        // Skip entries that do not apply to the last complete month.
        if (frequencyCode != PurchaseControlTable::FREQ_ALL && frequencyCode != lastMonthCode) {
            continue;
        }

        const bool found = std::any_of(invoices.cbegin(), invoices.cend(),
            [&](const PurchaseInformation &inv) {
                if (inv.accountSupplier != supplierAccount) {
                    return false;
                }
                if (!labelFilter.isEmpty()
                        && !inv.label.contains(labelFilter, Qt::CaseInsensitive)) {
                    return false;
                }
                return true;
            });

        if (!found) {
            QString line = supplierAccount;
            if (!labelFilter.isEmpty()) {
                line += QStringLiteral(" (") + labelFilter + QStringLiteral(")");
            }
            missingLines.append(line);
        }
    }

    if (missingLines.isEmpty()) {
        return;
    }

    const QString monthLabel = QLocale().monthName(lastMonthFirst.month())
                               + QStringLiteral(" ")
                               + QString::number(lastMonthFirst.year());

    QMessageBox::warning(
        this,
        tr("Missing Purchase Invoices"),
        tr("The following supplier invoices are expected for %1 but were not found:\n\n%2")
            .arg(monthLabel, missingLines.join('\n')));
}

bool PaneBookKeeping::displaySaleMissingWarning()
{
    const QDir workingDir = WorkingDirectoryManager::instance()->workingDir();

    const SaleControlTable controlTable{workingDir};
    if (controlTable.rowCount() == 0) {
        return true;
    }

    // Last complete month: first day of previous month → last day of previous month.
    const QDate today = QDate::currentDate();
    const QDate lastMonthFirst = QDate(today.year(), today.month(), 1).addMonths(-1);
    const QDate lastMonthLast  = lastMonthFirst.addMonths(1).addDays(-1);

    // Collect all stores that have at least one sale (positive total) and/or refund
    // (negative total) in the last complete month, across all activity sources.
    const auto activitySource_store_shipments =
        m_orderManager->getActivitySource_store_ShipmentAndRefunds(
            lastMonthFirst, lastMonthLast,
            [](const ActivitySource *, const Shipment *) { return true; });

    QSet<QString> storesWithSales;
    QSet<QString> storesWithRefunds;

    for (auto it = activitySource_store_shipments.cbegin();
         it != activitySource_store_shipments.cend(); ++it) {
        for (auto storeIt = it.value().cbegin();
             storeIt != it.value().cend(); ++storeIt) {
            const QString &store = storeIt.key();
            for (const auto &shipment : storeIt.value()) {
                if (shipment->getTotalTaxed() >= 0.0) {
                    storesWithSales.insert(store);
                } else {
                    storesWithRefunds.insert(store);
                }
            }
        }
    }

    QStringList missingLines;

    for (int row = 0; row < controlTable.rowCount(); ++row) {
        const QString storeName    = controlTable.data(controlTable.index(row, 0), Qt::EditRole).toString();
        const QString saleTypeCode = controlTable.data(controlTable.index(row, 1), Qt::EditRole).toString();

        bool missing = false;
        if (saleTypeCode == SaleControlTable::SALE_TYPE_BOTH) {
            missing = !storesWithSales.contains(storeName) || !storesWithRefunds.contains(storeName);
        } else if (saleTypeCode == SaleControlTable::SALE_TYPE_SALE) {
            missing = !storesWithSales.contains(storeName);
        } else if (saleTypeCode == SaleControlTable::SALE_TYPE_REFUND) {
            missing = !storesWithRefunds.contains(storeName);
        }

        if (missing) {
            missingLines.append(storeName
                + QStringLiteral(" (")
                + SaleControlTable::saleTypeDisplayText(saleTypeCode)
                + QStringLiteral(")"));
        }
    }

    if (missingLines.isEmpty()) {
        return true;
    }

    const QString monthLabel = QLocale().monthName(lastMonthFirst.month())
                               + QStringLiteral(" ")
                               + QString::number(lastMonthFirst.year());

    const auto answer = QMessageBox::question(
        this,
        tr("Missing Sales"),
        tr("The following stores have no expected activity for %1:\n\n%2\n\nContinue anyway?")
            .arg(monthLabel, missingLines.join('\n')),
        QMessageBox::Yes | QMessageBox::No);

    return answer == QMessageBox::Yes;
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

    QDir workingDir = WorkingDirectoryManager::instance()->workingDir();
    CompanyInfosTable companyInfo{workingDir};

    displayPurchaseMissingWarning();

    if (!displaySaleMissingWarning()) {
        co_return;
    }

    qDebug() << "[PaneBookKeeping] Associating tables...";
    // 2. Associate tables
    m_booksConnections->associateTablesToIds(getAllBookTables(), getSeflEntryTable(), &companyInfo);

    qDebug() << "[PaneBookKeeping] Preparing factory and dependencies...";
    BooksAccountsSalesTable salesAccountTable(workingDir); // Load sales accounts config
    BookAccountsGroupedSalesTable groupedSaleAccounts(workingDir); // Grouped client accounts per channel

    // Purchase Account Table needs company country code
    BookAccountPurchaseTable purchaseAccountTable(workingDir, companyInfo.getCompanyCountryCode());
    BookAccountSelfVatTable selfVatAccountTable(workingDir, companyInfo.getCompanyCountryCode());

    JournalTable journalTable(workingDir);
    AmzPaymentSettings amzPaymentSettings(workingDir);
    BookAccountAmzBalanceTable amzBalanceTable(workingDir);
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

    JournalEntryFactory factory(&currencyRateManager, &companyInfo, &salesAccountTable, &groupedSaleAccounts, &purchaseAccountTable, &journalTable, &selfVatAccountTable, &amzPaymentSettings, &amzBalanceTable);

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
        invoices = purchaseTable->getInvoices(from, to);
        qDebug() << "[PaneBookKeeping] Loaded Invoices. Count:" << invoices.size();
    } else {
        qDebug() << "[PaneBookKeeping] Warning: purchaseTable is null!";
    }

    qDebug() << "[PaneBookKeeping] Loading Amz Payments data...";
    auto *amzPaymentsTable = getAmzPaymentsTable();
    QList<AmzPaymentInfo> amzPayments;
    if (amzPaymentsTable) {
        amzPayments = amzPaymentsTable->getPayments(from, to);
        qDebug() << "[PaneBookKeeping] Loaded Amz Payments. Count:" << amzPayments.size();
    } else {
        qDebug() << "[PaneBookKeeping] Warning: amzPaymentsTable is null!";
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
    totalSteps += amzPayments.size();
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

        qDebug() << "[PaneBookKeeping] Sales completed. Generating OSS/IOSS VAT declaration entries...";
        // 5.1c OSS/IOSS VAT declaration entries — one set per complete trimester,
        //      all sources (grouped + ungrouped) combined so the declaration
        //      matches the full quarter's cross-border sales.
        //      Months 01+02+03 → entry dated 03/31, 04+05+06 → 06/30, etc.
        {
            // Collect every shipment from all sources, bucketed by calendar month
            QMap<QPair<int,int>, QMultiMap<QDateTime, QSharedPointer<Shipment>>> monthlyAllShipments;
            auto collectIntoMonthly = [&](const auto &sourceMap) {
                for (auto it = sourceMap.cbegin(); it != sourceMap.cend(); ++it) {
                    for (auto jt = it.value().cbegin(); jt != it.value().cend(); ++jt) {
                        const QDate d = jt.key().date();
                        monthlyAllShipments[{d.year(), d.month()}].insert(jt.key(), jt.value());
                    }
                }
            };
            collectIntoMonthly(sourceMapGrouped);
            collectIntoMonthly(sourceMapUngrouped);

            // Re-bucket months into quarters (Q1: 1-3, Q2: 4-6, Q3: 7-9, Q4: 10-12)
            QMap<QPair<int,int>, QMultiMap<QDateTime, QSharedPointer<Shipment>>> quarterlyAllShipments;
            for (auto it = monthlyAllShipments.cbegin(); it != monthlyAllShipments.cend(); ++it) {
                const int qYear   = it.key().first;
                const int qMonth  = it.key().second;
                const int quarter = (qMonth - 1) / 3 + 1;
                for (auto jt = it.value().cbegin(); jt != it.value().cend(); ++jt) {
                    quarterlyAllShipments[{qYear, quarter}].insert(jt.key(), jt.value());
                }
            }

            for (auto it = quarterlyAllShipments.cbegin(); it != quarterlyAllShipments.cend(); ++it) {
                const int qYear     = it.key().first;
                const int quarter   = it.key().second;
                const int lastMonth = quarter * 3;

                // Skip current or future quarters (complete only when last month has fully passed)
                if (qYear > currentDate.year()
                        || (qYear == currentDate.year() && lastMonth >= currentDate.month())) {
                    continue;
                }

                const QDate entryDate(qYear, lastMonth, QDate(qYear, lastMonth, 1).daysInMonth());
                const QDate declarationPeriod(qYear, lastMonth, 1);

                const QList<JournalEntryFactory::GroupedShipmentData> allGroups =
                    JournalEntryFactory::computeGrouping(nullptr, it.value(), entryDate);

                const QList<QSharedPointer<JournalEntry>> ossIossEntries =
                    co_await factory.createEntryOssIoss(
                        allGroups, entryDate, declarationPeriod, callbackAddIfMissing);

                for (const auto &entry : ossIossEntries) {
                    addEntry(entry, journalTable.getJournalVariousOperations().code);
                }
            }
        }
        qDebug() << "[PaneBookKeeping] OSS/IOSS entries completed. Starting purchases...";
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

        qDebug() << "[PaneBookKeeping] Purchases completed. Starting Amz Payments...";
        // 5.3 Amazon Payments
        for (const auto &info : std::as_const(amzPayments)) {
            if (progress.wasCanceled()) {
                qDebug() << "[PaneBookKeeping] Progress was canceled (Amz Payments)";
                co_return;
            }
            progress.setValue(currentStep++);

            QSharedPointer<JournalEntry> entry = co_await factory.createEntry(info, callbackAddIfMissing);
            const QString journalId = journalTable.getJournalAmzPayment().code;
            addEntry(entry, journalId);
        }

        qDebug() << "[PaneBookKeeping] Amz Payments completed. Starting Banks...";
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

                AbstractBooksTableBank *bankTableNonConst = const_cast<AbstractBooksTableBank*>(bankTable);
                QString account2 = m_booksConnections->getAccount2(bankTableNonConst, i);
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

                // Use the linked book-entry date for the currency rate so that both sides
                // of a cross-date connection always share the same conversion rate.
                const QDate linkedDate = m_booksConnections->getLinkedDate(bankTableNonConst, i);
                QSharedPointer<JournalEntry> entry = factory.createEntry(bankTable, account2, i, linkedDate);
                addEntry(entry, journal);
            }
        }

        // 5.4 Inventory moves (EU → company country intracom stock acquisitions)
        {
            const QStringList &countryCodes = CountriesEu::getAmazonPanEuCountryCodes();

            // Build per-country shipping costs from the WidgetPurchases configuration, per year
            QHash<int, QHash<QString, double>> country_pricePerKiloByYear;
            for (int y = from.year(); y <= to.year(); ++y) {
                for (const QString &cc : countryCodes) {
                    country_pricePerKiloByYear[y][cc] = ui->widgetPurchases->getShippingPrice(y, cc);
                }
                country_pricePerKiloByYear[y][""] = ui->widgetPurchases->getShippingPrice(y, ""); // catch-all default
            }

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
                                                country_pricePerKiloByYear,
                                                companyInfo.getCurrency(),
                                                &currencyRateManager,
                                                ui->widgetPurchases->getCsvFilePathsInventory(0),
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

        // Check if there are orders without invoices in the period.
        // Use `from` (not addYears(-1)) so that only orders with activity in the
        // selected year are shown.  Cross-year cases (e.g. a 2025 sale with a 2026
        // refund) are handled inside getShipmentAndRefundsNoInvoices: Phase 1 finds
        // the 2026 refund, Phase 2 fetches the 2025 sale together with it.
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
                DialogViewShipments dialog(*noInvoicesList, year, m_orderManager, this);
                if (dialog.exec() == QDialog::Accepted) {
                    QSet<QString> selected = dialog.getSelectedShipmentIds();
                    if (selected.size() == count) {
                        generateInvoicesWithSelection(std::nullopt);
                    } else {
                        generateInvoicesWithSelection(selected);
                    }
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
    int year = ui->comboBoxYear->currentText().toInt();
    QDate from(year, 1, 1);
    QDate to(year, 12, 31);

    auto noInvoicesList = m_orderManager->getShipmentAndRefundsNoInvoices(from, to);
    if (!noInvoicesList || noInvoicesList->isEmpty()) {
        QMessageBox::information(this, tr("No Invoices to Generate"),
            tr("All orders for %1 already have invoices.").arg(year));
        return;
    }

    int count = 0;
    for (const auto &entry : std::as_const(*noInvoicesList)) {
        for (bool needed : entry.invoicesToDo) {
            if (needed) {
                count++;
            }
        }
    }
    if (count == 0) {
        QMessageBox::information(this, tr("No Invoices to Generate"),
            tr("All orders for %1 already have invoices.").arg(year));
        return;
    }

    DialogViewShipments dialog(*noInvoicesList, year, m_orderManager, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QSet<QString> selected = dialog.getSelectedShipmentIds();
    if (selected.size() == count) {
        generateInvoicesWithSelection(std::nullopt);
    } else {
        generateInvoicesWithSelection(selected);
    }
}

void PaneBookKeeping::generateInvoicesWithSelection(std::optional<QSet<QString>> selectedShipmentIds)
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

    // 3. Retrieve orders without invoices for the period.
    // Use `from` (not addYears(-1)): cross-year cases (e.g. a 2025 sale paired
    // with a 2026 refund) are already handled inside the function — Phase 1 finds
    // the 2026 refund and Phase 2 pulls the entire order including the 2025 sale.
    // Using addYears(-1) here would additionally pull in standalone 2025 orders
    // that never got invoiced, generating 2025-dated invoices when 2026 is loaded.
    auto noInvoicesMap = m_orderManager->get_channel_site_ShipmentAndRefundsNoInvoices(from, to);
    if (!noInvoicesMap || noInvoicesMap->isEmpty()) {
        QMessageBox::information(this, tr("No Invoices to Generate"),
            tr("All orders for %1 already have invoices.").arg(year));
        return;
    }

    if (selectedShipmentIds.has_value()) {
        for (auto chanIt = (*noInvoicesMap).begin(); chanIt != (*noInvoicesMap).end(); ++chanIt) {
            for (auto storeIt = chanIt.value().begin(); storeIt != chanIt.value().end(); ++storeIt) {
                for (auto ctxIt = storeIt.value().begin(); ctxIt != storeIt.value().end(); ++ctxIt) {
                    auto &entry = ctxIt.value();
                    for (int i = 0; i < entry.shipmentsRefundsSameActivity.size(); ++i) {
                        if (entry.invoicesToDo.value(i, false)) {
                            if (entry.shipmentsRefundsSameActivity[i]
                                && !selectedShipmentIds->contains(entry.shipmentsRefundsSameActivity[i]->getId())) {
                                entry.invoicesToDo[i] = false;
                            }
                        }
                    }
                }
            }
        }
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
    QApplication::setOverrideCursor(Qt::WaitCursor);
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

                // Build per-entry shipment IDs, activity IDs, and dates so that:
                //  - Entries from the same order share a base number.
                //  - Entries from different orders each use their own date for the
                //    invoice number prefix (YYYYMM), preventing 2025 orders from
                //    receiving a 2026 invoice number when grouped with 2026 orders.
                QStringList shipmentIds;
                QStringList activityIds;
                QList<QDate> perEntryDates;
                shipmentIds.reserve(entry.shipmentsRefundsSameActivity.size());
                activityIds.reserve(entry.shipmentsRefundsSameActivity.size());
                perEntryDates.reserve(entry.shipmentsRefundsSameActivity.size());
                for (const auto &shipment : entry.shipmentsRefundsSameActivity) {
                    if (shipment && !shipment->getActivities().isEmpty()) {
                        shipmentIds.append(shipment->getActivities().first().getEventId());
                        activityIds.append(shipment->getActivities().first().getActivityId());
                        perEntryDates.append(shipment->getActivities().first().getDateTime().date());
                    } else {
                        shipmentIds.append(QString());
                        activityIds.append(QString());
                        perEntryDates.append(from);
                    }
                }

                // Generate invoice numbers for all shipments/refunds in this group.
                // Pass m_orderManager so that refunds whose sale invoice was imported
                // externally (e.g. Amazon FBA invoicing) receive -R01/-R02 suffixes
                // relative to the existing sale invoice rather than new base numbers.
                // Pass activityIds so that revision records store the per-activity ID
                // (e.g. "3105_refund") for correct invoicingInfo lookup on regeneration.
                QStringList invoiceNumbers = generator.getNextInvoiceNumbers(
                    date, taxContext, channel, store, entry.invoicesToDo, existingNumber, shipmentIds,
                    m_orderManager, activityIds, perEntryDates);

                // Fallback address when none is recorded (e.g. manual service sales)
                const Address emptyAddr("", "", "", "", "", "", "", "", "", "", "", "");

                for (int i = 0; i < entry.shipmentsRefundsSameActivity.size() && !cancelled; ++i) {
                    if (!entry.invoicesToDo.value(i, false)) continue;
                    if (progress.wasCanceled()) { cancelled = true; break; }
                    progress.setValue(currentStep++);

                    const QString &invoiceNumber = invoiceNumbers.value(i);
                    if (invoiceNumber.isEmpty()) continue;

                    const auto &shipment = entry.shipmentsRefundsSameActivity[i];
                    if (!shipment || shipment->getActivities().isEmpty()) {
                        errors++;
                        errorMsgs << tr("Empty activities");
                        generator.removeInvoiceByNumber(invoiceNumber);
                        continue;
                    }

                    const QString &orderId = shipment->getActivities().first().getEventId();
                    const QString &activityId = shipment->getActivities().first().getActivityId();
                    if (activityId.isEmpty()) {
                        errors++;
                        errorMsgs << tr("Empty shipmentId");
                        generator.removeInvoiceByNumber(invoiceNumber);
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
                        generator.removeInvoiceByNumber(invoiceNumber);
                        continue;
                    }

                    // Propagate the order currency so the PDF can show original + converted amounts.
                    const QString &orderCurrency = shipment->getActivities().first().getCurrency();
                    if (!orderCurrency.isEmpty() && info->getCurrency().isEmpty()) {
                        info->setCurrency(orderCurrency);
                    }

                    const Address &addressTo = entry.addressTo ? *entry.addressTo : emptyAddr;
                    auto invoiceDate = shipment->getActivities().first().getDateTime().date();

                    // Sanitize invoice number for use as a filename
                    QString sanitized = invoiceNumber;
                    sanitized.replace('/', '-').replace('\\', '-');
                    QString subDirName = QString("%1/%2").arg(invoiceDate.year()).arg(invoiceDate.month(), 2, 10, QChar('0'));
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
                        // Pass activityId (shipment root ID) as the recording key so that
                        // invoicing info is stored under the shipment root, not the Amazon
                        // order ID. This ensures Phase-2 lookups in
                        // get_channel_site_ShipmentAndRefundsNoInvoices find the invoice
                        // via JOIN on COALESCE(root_id, id) = shipment_root_id.
                        generator.generateInvoice(invoiceNumber, prevNumber, pdfPath,
                                                   addressTo, *info, orderId, *m_orderManager,
                                                   invoiceDate, activityId);
                        if (QFile::exists(pdfPath)) {
                            generated++;
                        } else {
                            // PDF was not written (e.g. disk full, invalid path).
                            // Remove the prematurely-saved CSV record so the system
                            // does not consider this invoice as having been generated.
                            errors++;
                            errorMsgs << tr("PDF not created for %1").arg(orderId);
                            generator.removeInvoiceByNumber(invoiceNumber);
                        }
                    } catch (const std::exception &ex) {
                        qWarning() << "[generateInvoices] Failed for" << orderId << ":" << ex.what();
                        errors++;
                        errorMsgs << tr("Failed for %1: %2").arg(orderId, ex.what());
                        // Remove the CSV record allocated by getNextInvoiceNumbers so it
                        // is not left as a ghost entry (present in invoices.csv but absent
                        // from invoicing_infos and without a matching PDF on disk).
                        generator.removeInvoiceByNumber(invoiceNumber);
                    }
                }
            }
        }
    }

    progress.setValue(totalSteps);
    QApplication::restoreOverrideCursor();

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
    {
        QProgressDialog progress(tr("Regenerating invoices for %1...").arg(year),
                                 QString(), 0, 0, this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QApplication::processEvents();

        try {
            generator.regenerateInvoices(outDir, from, to, *m_orderManager);
            QApplication::restoreOverrideCursor();

            QMessageBox::information(
                this,
                tr("Regeneration Complete"),
                tr("Invoices for %1 have been regenerated in:\n%2")
                    .arg(year)
                    .arg(outDir.absolutePath()));
        } catch (const ExceptionWithTitleText &e) {
            QApplication::restoreOverrideCursor();
            QMessageBox::warning(this, e.errorTitle(), e.errorText());
        }
    }
}

void PaneBookKeeping::generateReports()
{
    // 1. Ask for the output folder
    QSettings settings;
    QString lastDir = settings.value("lastInvoicesDir", QDir::homePath()).toString();
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Reports Output Folder"), lastDir);
    if (dir.isEmpty()) {
        return;
    }
    settings.setValue("lastInvoicesDir", dir);
    QDir outDir(dir);

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
    const QString companyCurrency = companyInfos.getCurrency();
    const QString apiKey = companyInfos.getApiKeyFixer();
    CurrencyRateManager currencyRateManager(workingDir, apiKey);
    CompanyAddressTable companyAddress(workingDir);
    VatNumbersTable vatNumbers(workingDir);
    InvoiceGenerator invoiceGen(workingDir, &companyInfos, &companyAddress,
                                &currencyRateManager, &vatNumbers);

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

    // Count processable months for the progress dialog
    const QDate today = QDate::currentDate();
    int totalMonths = 0;
    for (int m = 1; m <= 12; ++m) {
        if (!(year > today.year() || (year == today.year() && m >= today.month()))) {
            ++totalMonths;
        }
    }

    QProgressDialog progress(tr("Generating sale reports..."), QString(), 0, totalMonths, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int progressStep = 0;

    for (int month = 1; month <= 12; ++month) {
        // Skip current and future months (data may be incomplete)
        if (year > today.year() || (year == today.year() && month >= today.month())) {
            continue;
        }

        progress.setValue(progressStep++);
        QApplication::processEvents();

        const QString monthStr = QString("%1").arg(month, 2, 10, QChar('0'));

        // One ReportGenerator per channel (key = channel name, e.g. "Amazon", "Temu")
        QMap<QString, ReportGenerator> channelReports;

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
            if (monthlyShipments.isEmpty()) {
                continue;
            }

            const QString &channelKey = source.channel;

            // Initialise the report for this channel on first use
            if (!channelReports.contains(channelKey)) {
                ReportGenerator &r = channelReports[channelKey];
                r.setLandscape(true);
                r.setPageSize(QPageSize::A3);
                r.setFontScale(1.5);
                r.addTitle(tr("Sale %1").arg(channelKey) +
                           QString(" %1/%2").arg(year).arg(monthStr));
            }
            ReportGenerator &report = channelReports[channelKey];

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
                    if (group.vatRatePct > 0.01 && !group.vatAccount.isEmpty()) {
                        subtitle += " / " + group.vatAccount;
                    }
                    subtitle += "]";
                }
                report.addSubtitle(subtitle);

                report.startTable(tableHeaders);

                // Sort rows by store then date
                QList<JournalEntryFactory::ShipmentReportInfo> rows = group.shipments;
                std::sort(rows.begin(), rows.end(),
                    [](const JournalEntryFactory::ShipmentReportInfo &a,
                       const JournalEntryFactory::ShipmentReportInfo &b) {
                        if (a.store != b.store) { return a.store < b.store; }
                        return a.date < b.date;
                    });

                // Convert main amounts to company currency; orig fields keep source values.
                for (auto &row : rows) {
                    if (row.currency != companyCurrency) {
                        row.untaxedAmount = currencyRateManager.convert(row.untaxedAmount, row.currency, companyCurrency, row.date);
                        row.taxes         = currencyRateManager.convert(row.taxes,         row.currency, companyCurrency, row.date);
                        row.taxedAmount   = currencyRateManager.convert(row.taxedAmount,   row.currency, companyCurrency, row.date);
                        row.currency      = companyCurrency;
                    }
                }

                double sumUntaxed = 0.0, sumTaxes = 0.0, sumTaxed = 0.0;

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
                            const auto link = info->getInvoiceLink()->trimmed();
                            if (!link.isEmpty()) {
                                invoiceLinkHtml =
                                    "<a href=\"" + *info->getInvoiceLink() + "\">" + tr("Open") + "</a>";
                            }
                        }
                        // Fallback: invoicing_infos DB may be missing the entry (e.g. generateInvoice
                        // threw after _save() had already written the record to invoices.csv).
                        if (invoiceNumberStr.isEmpty()) {
                            invoiceNumberStr =
                                invoiceGen.getInvoiceNumberForActivityId(row.shipmentRefundId);
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
                totalRow[8]  = companyCurrency;
                report.addRowTotal(totalRow);

                report.endTable();
            }
        }

        if (!channelReports.isEmpty()) {
            const QString subDirName = QString("%1/%2").arg(year).arg(monthStr);
            QDir subDir(dirTo.filePath(subDirName));
            subDir.mkpath(".");
            for (auto it = channelReports.cbegin(); it != channelReports.cend(); ++it) {
                const QString channelFilename = it.key().toLower();
                const QString reportPath = subDir.absoluteFilePath(
                    QString("sale_%1_%2_%3.pdf").arg(channelFilename).arg(year).arg(monthStr));
                try {
                    it.value().save(reportPath);
                } catch (const std::exception &ex) {
                    qWarning() << "[generateSaleReports] Failed to save report:" << ex.what();
                }
            }
        }
    }

    progress.setValue(totalMonths);
    QApplication::restoreOverrideCursor();
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
            unselectAll();
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

        // Add selections from other bank tabs in toolBoxBanks
        QList<QTableView *> allBankViews = ui->toolBoxBanks->findChildren<QTableView *>();
        for (QTableView *bankTabView : std::as_const(allBankViews)) {
            if (bankTabView == bankView) continue; // already added
            auto *otherBankTable = qobject_cast<AbstractBooksTableBank*>(bankTabView->model());
            if (!otherBankTable) continue;
            QModelIndexList sel = bankTabView->selectionModel()->selectedRows();
            if (!sel.isEmpty()) {
                tableIndexes.insert(otherBankTable, sel);
            }
        }

        // For each book table, get its selection
        for (const AbstractBooksTable *bookTable : std::as_const(bookTables)) {
            // Find the QTableView for this book table
            QList<QTableView *> allViews = ui->toolBoxSalePurchases->findChildren<QTableView *>();
            for (QTableView *view : std::as_const(allViews)) {
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

        // Check if we have at least two rows selected in total
        int totalRows = 0;
        for (const auto &sel : tableIndexes)
            totalRows += sel.size();
        if (totalRows < 2) {
            QMessageBox::warning(this, tr("Insufficient Selection"),
                tr("Please select at least two rows (from one or more tables) to associate."));
            return;
        }

        // Create CurrencyRateManager for the connection (only needed for cross-currency).
        // Same-currency associations work without an API key; tryToConnect will throw
        // with a clear error if a rate lookup is actually required but no key is set.
        QDir workingDir = WorkingDirectoryManager::instance()->workingDir();
        CompanyInfosTable companyInfo{workingDir};
        const auto &apiKey = companyInfo.getApiKeyFixer();

        try {
            if (apiKey.isEmpty()) {
                m_booksConnections->tryToConnect(tableIndexes, nullptr);
            } else {
                CurrencyRateManager currencyRateManager{workingDir, apiKey};
                m_booksConnections->tryToConnect(tableIndexes, &currencyRateManager);
            }
            unselectAll();
        } catch (const std::exception &e) {
            QMessageBox::warning(this, tr("Association Failed"),
                                 tr("Failed to associate entries: %1").arg(e.what()));
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
        const CurrencyRateManager currencyRateManager{WorkingDirectoryManager::instance()->workingDir(), companyInfos.getApiKeyFixer()};

        PurchaseInformation info = PurchaseInvoiceManager::decode(fi.fileName(), &purchaseAccountTable, companyInfos.getCompanyCountryCode());

        while (true) {
            DialogEditPurchase editDialog(info, companyCurrency, &purchaseAccountTable, &currencyRateManager, this);
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
                if (purchaseTable->isSupplierWithCountries(info.accountSupplier)) {
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

        purchaseTable->addInvoice(fileName, info);
        // Reload table for the year of the invoice
        //purchaseTable->load(info.date.year());

        // Also ensure UI year is correct? Or just warn if added to a different year?
        if (ui->comboBoxYear->currentText().toInt() != info.date.year()) {
            QMessageBox::information(this, tr("Invoice Added"),
                tr("Invoice added to year %1 (Current view is %2). Switch year to view it.")
                .arg(info.date.year())
                .arg(ui->comboBoxYear->currentText()));
        } else {
             // Refresh current view
             //purchaseTable->load(info.date.year());
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
    const CurrencyRateManager currencyRates{WorkingDirectoryManager::instance()->workingDir(), companyInfos.getApiKeyFixer()};

    DialogEditPurchases dialog(&purchaseAccountTable, fileNames, companyCurrency, &currencyRates, this);
    if (dialog.exec() == QDialog::Accepted) {
        auto purchaseTable = static_cast<PurchaseInvoiceTable *>(ui->tableInvoices->model());
        QList<PurchaseInformation> invoicesToAdd = dialog.getInfos();

        // Loop 1: validate — ask about missing countries without adding anything yet
        bool aborted = false;
        QSet<QString> confirmedSuppliers; // suppliers already confirmed once
        for (const PurchaseInformation &info : std::as_const(invoicesToAdd)) {
            if (info.countryCodeFrom.isEmpty() && info.countryCodeTo.isEmpty()) {
                if (!confirmedSuppliers.contains(info.accountSupplier)
                        && purchaseTable->isSupplierWithCountries(info.accountSupplier)) {
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
                purchaseTable->addInvoice(info.filePath, info);
                count++;
            } catch (...) {
                errCount++;
            }
        }

        // Reload current view
        //purchaseTable->load(ui->comboBoxYear->currentText().toInt());

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

    auto answer = QMessageBox::question(this, tr("Remove invoices ?"),
        tr("Are you sure you want to remove the %1 selected invoice(s) ?").arg(selection.size()));

    if (answer == QMessageBox::Yes) {
        // Remove in reverse order so indexes remain valid
        for (int i = selection.size() - 1; i >= 0; --i) {
            purchaseTable->removeInvoice(selection[i]);
        }
    }
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

    const QString warning = stmt->hasWarnings(fileName);
    if (!warning.isEmpty()) {
        if (QMessageBox::question(this, tr("Import Warning"), warning) != QMessageBox::Yes) {
            return;
        }
    }

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
    while (dialog.exec() == QDialog::Accepted) {
        try {
            serviceTable->createSale(
                &clientManager,
                dialog.getSelectedClientRow(),
                dialog.getDate(),
                dialog.getCurrency(),
                dialog.getInvoiceId(),
                dialog.getAccount(),
                dialog.getLineItems(),
                vatResolver,
                taxResolver,
                dialog.getPaymentType(),
                dialog.getPaymentDays(),
                dialog.getVatOnPayment(),
                onMissingVatRate
            );
            break;
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

void PaneBookKeeping::serviceEditSale()
{
    auto *serviceTable = static_cast<ServiceSalesBooksTable *>(ui->tableServices->model());
    if (!serviceTable) {
        return;
    }

    const QModelIndexList selection = ui->tableServices->selectionModel()->selectedRows();
    if (selection.isEmpty()) {
        QMessageBox::warning(this, tr("No Selection"), tr("Please select a sale to edit."));
        return;
    }

    const int row = selection.first().row();
    const QString rowId = serviceTable->getRowId(serviceTable->index(row, 0));

    if (m_orderManager->isOrderPublished(rowId)) {
        QMessageBox::warning(this, tr("Cannot Edit Sale"),
                             tr("The sale \"%1\" has been published and cannot be modified.").arg(rowId));
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
    dialog.setWindowTitle(tr("Edit Service Sale"));
    dialog.setDate(serviceTable->getDate(row));
    dialog.setReference(serviceTable->data(serviceTable->index(row, ServiceSalesBooksTable::IND_REFERENCE), Qt::EditRole).toString());
    dialog.setClientByServiceLabel(serviceTable->getLabel(row));
    dialog.setVatOnPayment(serviceTable->data(serviceTable->index(row, ServiceSalesBooksTable::IND_VAT_ON_PAYMENT), Qt::EditRole).toBool());
    dialog.setPaymentTermFromString(serviceTable->data(serviceTable->index(row, ServiceSalesBooksTable::IND_PAYMENT_TERM), Qt::EditRole).toString());
    dialog.setLineItems(serviceTable->getLineItems(rowId));

    QDir workingDir{WorkingDirectoryManager::instance()->workingDir()};
    CompanyInfosTable companyInfos(workingDir);
    CompanyAddressTable companyAddress(workingDir);
    const QString apiKey = companyInfos.getApiKeyFixer();
    CurrencyRateManager currencyRates(workingDir, apiKey);
    VatNumbersTable vatNumbers(workingDir);
    InvoiceGenerator generator(workingDir, &companyInfos, &companyAddress, &currencyRates, &vatNumbers);

    while (dialog.exec() == QDialog::Accepted) {
        try {
            serviceTable->setInvoiceGenerator(&generator);
            serviceTable->replaceSale(
                rowId,
                &clientManager,
                dialog.getSelectedClientRow(),
                dialog.getDate(),
                dialog.getCurrency(),
                dialog.getInvoiceId(),
                dialog.getAccount(),
                dialog.getLineItems(),
                vatResolver,
                taxResolver,
                dialog.getPaymentType(),
                dialog.getPaymentDays(),
                dialog.getVatOnPayment(),
                onMissingVatRate
            );
            serviceTable->setInvoiceGenerator(nullptr);
            break;
        } catch (const ExceptionWithTitleText &e) {
            serviceTable->setInvoiceGenerator(nullptr);
            QMessageBox::warning(this, e.errorTitle(), e.errorText());
        } catch (const std::exception &e) {
            serviceTable->setInvoiceGenerator(nullptr);
            QMessageBox::warning(this, tr("Error"), tr("An error occurred: %1").arg(e.what()));
        }
    }
}

void PaneBookKeeping::serviceReInvoice()
{
    auto *serviceTable = static_cast<ServiceSalesBooksTable *>(ui->tableServices->model());
    if (!serviceTable) {
        return;
    }

    const QModelIndexList selection = ui->tableServices->selectionModel()->selectedRows();
    if (selection.isEmpty()) {
        QMessageBox::warning(this, tr("No Selection"),
                             tr("Please select a published sale to re-invoice."));
        return;
    }

    const int row = selection.first().row();
    const QString rowId = serviceTable->getRowId(serviceTable->index(row, 0));

    if (!m_orderManager->isOrderPublished(rowId)) {
        QMessageBox::warning(this, tr("Sale Not Published"),
                             tr("The sale \"%1\" has not been published. "
                                "Re-invoicing only applies to published sales.").arg(rowId));
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
    dialog.setWindowTitle(tr("Re-invoice Sale"));
    dialog.setDate(serviceTable->getDate(row));
    dialog.setReference(serviceTable->data(serviceTable->index(row, ServiceSalesBooksTable::IND_REFERENCE), Qt::EditRole).toString());
    dialog.setClientByServiceLabel(serviceTable->getLabel(row));
    dialog.setVatOnPayment(serviceTable->data(serviceTable->index(row, ServiceSalesBooksTable::IND_VAT_ON_PAYMENT), Qt::EditRole).toBool());
    dialog.setPaymentTermFromString(serviceTable->data(serviceTable->index(row, ServiceSalesBooksTable::IND_PAYMENT_TERM), Qt::EditRole).toString());
    dialog.setLineItems(serviceTable->getLineItems(rowId));

    while (dialog.exec() == QDialog::Accepted) {
        try {
            serviceTable->replacePublishedSale(
                rowId,
                &clientManager,
                dialog.getSelectedClientRow(),
                dialog.getDate(),
                dialog.getCurrency(),
                dialog.getInvoiceId(),
                dialog.getAccount(),
                dialog.getLineItems(),
                vatResolver,
                taxResolver,
                dialog.getPaymentType(),
                dialog.getPaymentDays(),
                dialog.getVatOnPayment(),
                onMissingVatRate
            );
            break;
        } catch (const ExceptionWithTitleText &e) {
            QMessageBox::warning(this, e.errorTitle(), e.errorText());
        } catch (const std::exception &e) {
            QMessageBox::warning(this, tr("Error"), tr("An error occurred: %1").arg(e.what()));
        }
    }
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

    dialog.setFirstArticleUnitPrice(qAbs(totalAmount));
    dialog.setReference(labels.join(" + "));
    dialog.setDate(date);

    if (dialog.exec() == QDialog::Accepted) {
        auto *serviceTable = static_cast<ServiceSalesBooksTable *>(ui->tableServices->model());
        if (serviceTable) {
            serviceTable->createSale(
                &clientManager,
                dialog.getSelectedClientRow(),
                dialog.getDate(),
                dialog.getCurrency(),
                dialog.getInvoiceId(),
                dialog.getAccount(),
                dialog.getLineItems(),
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

void PaneBookKeeping::saleAddRefund()
{
    QCoro::connect(saleAddRefundAsync(), this, []() {});
}

QCoro::Task<> PaneBookKeeping::saleAddRefundAsync()
{
    auto *ecomTable = qobject_cast<UngroupedOrderTable *>(ui->tableEcomSales->model());
    if (!ecomTable) {
        co_return;
    }

    const QModelIndexList selection = ui->tableEcomSales->selectionModel()->selectedRows();
    if (selection.size() != 1) {
        QMessageBox::warning(this, tr("Selection Error"),
                             tr("Please select exactly one order to refund."));
        co_return;
    }

    const int row = selection.first().row();
    const QString orderId   = ecomTable->getRowId(ecomTable->index(row, 0));
    const double amount     = ecomTable->data(ecomTable->index(row, AbstractBooksTable::IND_AMOUNT)).toDouble();
    const QString currency  = ecomTable->data(ecomTable->index(row, AbstractBooksTable::IND_CURRENCY)).toString();

    if (amount <= 0.0) {
        QMessageBox::warning(this, tr("Invalid Selection"),
                             tr("The selected entry is already a refund (negative amount). "
                                "Please select a sale entry."));
        co_return;
    }

    const QDate today       = QDate::currentDate();
    const QDate defaultDate = QDate(today.year(), today.month(), 1).addMonths(-1);

    const auto invoicingInfo = m_orderManager->getInvoicingInfoByOrderId(orderId);
    const QList<LineItem> items = invoicingInfo ? invoicingInfo->getItems() : QList<LineItem>{};

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Add Refund — %1").arg(orderId));

    auto *form       = new QFormLayout;
    auto *dateEdit   = new QDateEdit(defaultDate, &dlg);
    dateEdit->setCalendarPopup(true);
    dateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    form->addRow(tr("Refund date:"), dateEdit);

    auto *refundIdLineEdit = new QLineEdit(orderId + QStringLiteral("-refund"), &dlg);
    form->addRow(tr("Refund ID:"), refundIdLineEdit);

    // Case A: no items — simple amount spinbox (existing behaviour)
    QDoubleSpinBox *amountSpin = nullptr;
    // Case B: items available — table with per-item refund spinboxes
    QList<QDoubleSpinBox *> itemSpinBoxes;
    QLabel *totalLabel = nullptr;

    if (items.isEmpty()) {
        amountSpin = new QDoubleSpinBox(&dlg);
        amountSpin->setRange(0.01, amount);
        amountSpin->setDecimals(2);
        amountSpin->setSingleStep(0.01);
        amountSpin->setValue(amount);
        amountSpin->setSuffix(QStringLiteral(" ") + currency);
        form->addRow(tr("Amount to refund:"), amountSpin);
    } else {
        auto *table = new QTableWidget(items.size(), 4, &dlg);
        table->setHorizontalHeaderLabels({tr("Item"), tr("Qty"), tr("Full amount"), tr("Refund amount")});
        table->horizontalHeader()->setStretchLastSection(true);
        table->verticalHeader()->setVisible(false);

        totalLabel = new QLabel(&dlg);

        for (int i = 0; i < items.size(); ++i) {
            const auto &item = items[i];

            auto *itemName = new QTableWidgetItem(item.getName());
            itemName->setFlags(itemName->flags() & ~Qt::ItemIsEditable);
            table->setItem(i, 0, itemName);

            auto *itemQty = new QTableWidgetItem(QString::number(item.getQuantity()));
            itemQty->setFlags(itemQty->flags() & ~Qt::ItemIsEditable);
            table->setItem(i, 1, itemQty);

            auto *itemAmount = new QTableWidgetItem(QString::number(item.getTotalTaxed(), 'f', 2));
            itemAmount->setFlags(itemAmount->flags() & ~Qt::ItemIsEditable);
            table->setItem(i, 2, itemAmount);

            auto *spin = new QDoubleSpinBox(&dlg);
            spin->setRange(0.0, item.getTotalTaxed());
            spin->setDecimals(2);
            spin->setSingleStep(0.01);
            spin->setValue(item.getTotalTaxed());
            table->setCellWidget(i, 3, spin);
            itemSpinBoxes.append(spin);
        }

        auto updateTotal = [totalLabel, &itemSpinBoxes, currency]() {
            double total = 0.0;
            for (const auto *spin : std::as_const(itemSpinBoxes)) {
                total += spin->value();
            }
            totalLabel->setText(QObject::tr("Total refund: %1 %2").arg(QString::number(total, 'f', 2), currency));
        };
        updateTotal();

        for (auto *spin : std::as_const(itemSpinBoxes)) {
            connect(spin, &QDoubleSpinBox::valueChanged, &dlg, updateTotal);
        }

        table->resizeColumnsToContents();
        form->addRow(table);
        form->addRow(totalLabel);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) {
        co_return;
    }

    const QDate refundDate = dateEdit->date();
    const QString customRefundId = refundIdLineEdit->text().trimmed();

    double refundAmount = 0.0;
    QList<LineItem> refundItems;

    if (amountSpin) {
        refundAmount = amountSpin->value();
    } else {
        for (int i = 0; i < items.size(); ++i) {
            const double newTotal = itemSpinBoxes.at(i)->value();
            refundAmount += newTotal;
            if (qAbs(newTotal) < 0.001) {
                continue;
            }
            const double qty = items.at(i).getQuantity();
            if (qAbs(qty) < 0.001) {
                continue;
            }
            const double newTaxedPerUnit = newTotal / qty;
            const double untaxedPerUnit = items.at(i).getAmountUntaxed();
            const double vatRate = (qAbs(untaxedPerUnit) > 0.001)
                ? (items.at(i).getAmountTaxed() - untaxedPerUnit) / untaxedPerUnit
                : 0.0;
            const auto res = LineItem::create(items.at(i).getSku(), items.at(i).getName(),
                                              newTaxedPerUnit, vatRate, qty);
            if (res.ok()) {
                refundItems.append(*res.value);
            }
        }
    }

    const QString errorMsg = m_orderManager->recordManualRefund(
        orderId, customRefundId, -refundAmount, currency, refundDate, refundItems);

    if (!errorMsg.isEmpty()) {
        QMessageBox::warning(this, tr("Refund Error"), errorMsg);
        co_return;
    }

    loadYearSelected();
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

void PaneBookKeeping::_deleteBooksTables()
{
    auto allBookTables = getAllBookTables();
    for (auto bookTable : allBookTables) {
        if (bookTable != nullptr) {
            QTableView *tableView = dynamic_cast<QTableView *>(bookTable->parent());
            if (tableView != nullptr) {
                tableView->setModel(nullptr);
                bookTable->deleteLater();
            }
        }
    }
}

void PaneBookKeeping::_createBooksTables()
{
    _createBanks();
    QDir workingDir{WorkingDirectoryManager::instance()->workingDir()};

    // Self entries
    if (ui->tableSelfEntry->model() == nullptr) {
        auto selfEntriesTable = new EntrySelfTable(workingDir, ui->tableSelfEntry);
        ui->tableSelfEntry->setModel(selfEntriesTable);
        ui->tableSelfEntry->horizontalHeader()->resizeSection(0, 250);
    }

    // Purchases
    const CompanyInfosTable companyInfosForTable{workingDir};
    auto purchaseTable = new PurchaseInvoiceTable(m_booksConnections, workingDir, companyInfosForTable.getCompanyCountryCode(), ui->tableInvoices);
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

    // E-commerce (ungrouped) orders
    auto ecomSalesBooksAccounts = new BooksAccountsSalesTable(workingDir, ui->tableEcomSales);
    auto ecomSalesTable = new UngroupedOrderTable(
        m_booksConnections, m_orderManager, workingDir,
        ecomSalesBooksAccounts,
        companyInfosForTable.getCompanyCountryCode(),
        companyInfosForTable.getCurrency(),
        ui->tableEcomSales);
    ui->tableEcomSales->setModel(ecomSalesTable);
    ui->tableEcomSales->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableEcomSales->setSelectionMode(QAbstractItemView::ExtendedSelection);
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
    connect(ui->buttonGenerateReports,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::generateReports);
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
    connect(ui->buttonServiceEditSale,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::serviceEditSale);
    connect(ui->buttonServiceReInvoice,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::serviceReInvoice);
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
    connect(ui->buttonEcomRefund,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::saleAddRefund);
}

void PaneBookKeeping::_updateServiceButtonsEnabled()
{
    ServiceClientManager clientManager(WorkingDirectoryManager::instance()->workingDir());
    bool hasClients = clientManager.rowCount() > 0;
    ui->buttonServiceAdd->setEnabled(hasClients);
    ui->buttonServiceRemove->setEnabled(hasClients);
    ui->buttonServiceEditSale->setEnabled(hasClients);
    ui->buttonServiceReInvoice->setEnabled(hasClients);
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
