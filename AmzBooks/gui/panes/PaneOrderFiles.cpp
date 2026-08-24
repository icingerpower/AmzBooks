#include <QFileDialog>
#include <QMessageBox>
#include <QDebug>
#include <QCoroTask>
#include <QException>
#include <QSettings>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QSet>

#include "../../../../common/workingdirectory/WorkingDirectoryManager.h"

#include "orders/FileImportersTable.h"
#include "orders/ParamsTable.h"
#include "orders/AbstractImporterFile.h"
#include "orders/OrderManager.h"
#include "ExceptionWithTitleText.h"

#include "PaneOrderFiles.h"
#include "ui_PaneOrderFiles.h"
#include "gui/dialogs/DialogVatParams.h"
#include "gui/dialogs/DialogVatParams.h"
#include "gui/dialogs/DialogViewOrders.h"
#include "gui/dialogs/DialogPickShipment.h"
#include "utils/CsvHeader.h"
#include "books/ActivityTable.h"
#include "books/CompanyInfosTable.h"
#include "books/TaxResolver.h"
#include "books/VatResolver.h"
#include "books/VatTerritoryResolver.h"
#include "CurrencyRateManager.h"

#include <QFileSystemModel>

PaneOrderFiles::PaneOrderFiles(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PaneOrderFiles),
    m_importersTable(new FileImportersTable(this)),
    m_fileSystemModel(new QFileSystemModel(this))
{
    ui->setupUi(this);

    // Setup Importers Table
    ui->tableImporters->setModel(m_importersTable);
    ui->tableImporters->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tableImporters->horizontalHeader()->setStretchLastSection(true);
    ui->tableImporters->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableImporters->setSelectionMode(QAbstractItemView::SingleSelection);

    // Setup Params Table (Headers will be set when model is set)
    ui->tableParams->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    // Setup Params Table (Headers will be set when model is set)
    ui->tableParams->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    ui->tableParams->horizontalHeader()->setStretchLastSection(true);

    // Setup Orders Table
    ui->tableOrders->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    ui->tableOrders->horizontalHeader()->setStretchLastSection(true);
    ui->tableOrders->setSortingEnabled(true);

    _connectSlots();

    ui->tableImporters->setCurrentIndex(ui->tableImporters->model()->index(0, 0));
}

PaneOrderFiles::~PaneOrderFiles()
{
    delete ui;
}

void PaneOrderFiles::_connectSlots()
{
    connect(ui->buttonImport, &QPushButton::clicked, this, &PaneOrderFiles::importFile);
    connect(ui->buttonRemove, &QPushButton::clicked, this, &PaneOrderFiles::removeFile);

    connect(ui->tableImporters->selectionModel(),
            &QItemSelectionModel::currentRowChanged,
            this,
            &PaneOrderFiles::onImporterSelected);
}

void PaneOrderFiles::_refreshImportedFilesList(AbstractImporterFile *importer)
{
    ui->listImportedFiles->clear();
    if (!importer) {
        return;
    }

    QSet<QString> seen;

    // Part 1 — QSettings: Reports/ImportedIds stores filenames, oldest first (append order).
    // Reverse for newest-first display.
    {
        const QStringList ids = importer->getImportedIds();
        for (int i = ids.size() - 1; i >= 0; --i) {
            const QString &fname = ids.at(i);
            if (seen.contains(fname)) {
                continue;
            }
            seen.insert(fname);
            ui->listImportedFiles->addItem(fname);
        }
    }

    // Part 2 — import_log.csv: backward-compat for entries that pre-date the QSettings fix.
    // Filter by importer label, skip filenames already added from QSettings.
    {
        const QDir workingDir(WorkingDirectoryManager::instance()->workingDir());
        const QString logPath = workingDir.absoluteFilePath("import_log.csv");
        QFile logFile(logPath);
        if (logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&logFile);
            in.setEncoding(QStringConverter::Utf8);
            if (!in.atEnd()) {
                in.readLine(); // skip header
            }
            const QString importerLabel = importer->getLabel();
            QList<QPair<QDateTime, QString>> logEntries;
            while (!in.atEnd()) {
                const QString line = in.readLine();
                const QStringList parts = line.split(';');
                if (parts.size() < 3 || parts.at(1) != importerLabel) {
                    continue;
                }
                const QString fname = parts.at(2);
                if (seen.contains(fname)) {
                    continue;
                }
                seen.insert(fname);
                logEntries.append(qMakePair(QDateTime::fromString(parts.at(0), Qt::ISODate), fname));
            }
            std::sort(logEntries.begin(), logEntries.end(), [](const auto &a, const auto &b) {
                return a.first > b.first;
            });
            for (const auto &entry : std::as_const(logEntries)) {
                ui->listImportedFiles->addItem(entry.second);
            }
        }
    }
}

void PaneOrderFiles::onImporterSelected(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous);

    if (!current.isValid()) {
        ui->tableParams->setModel(nullptr);
        ui->treeViewFiles->setModel(nullptr);
        ui->listImportedFiles->clear();
        if (m_paramsModel) {
            delete m_paramsModel;
            m_paramsModel = nullptr;
        }
        return;
    }

    AbstractImporterFile *importer = m_importersTable->getImporter(current);
    if (!importer) {
        ui->tableParams->setModel(nullptr);
        ui->treeViewFiles->setModel(nullptr);
        ui->listImportedFiles->clear();
        return;
    }

    importer->setWorkingDirectory(QDir(AbstractImporterFile::GET_WORKING_DIR(
        WorkingDirectoryManager::instance()->workingDir(), importer->getId())));

    // Ensure params are loaded/initialized if not already
    // importer->load(); // Assuming load() is cheap or idempotent. 
    // Actually FileImportersTable might not load them by default.
    // AbstractImporter::load() accesses settings.
    
    // Create new ParamsTable
    // Note: We create a new model each time selection changes.
    auto *oldModel = m_paramsModel;
    m_paramsModel = new ParamsTable(importer, this);
    connect(m_paramsModel, &ParamsTable::exceptionOccurred, this, [this](const QString &title, const QString &message) {
        QMessageBox::warning(this, title, message);
    });
    ui->tableParams->setModel(m_paramsModel);
    ui->tableParams->setVisible(m_paramsModel->rowCount() > 0);
    
    // Setup Activity Table for Importer
    if (!m_activityModels.contains(importer->getId())) {
        m_activityModels[importer->getId()] = new ActivityTable(this);
    }
    ui->tableOrders->setModel(m_activityModels[importer->getId()]);
    
    if (oldModel) {
        oldModel->deleteLater();
    }

    // Init treeViewFiles
    QString workingDir = AbstractImporterFile::GET_WORKING_DIR(
        WorkingDirectoryManager::instance()->workingDir(), 
        importer->getId()
    );
    
    // Create directory if it doesn't exist? Optional but good practice if expected.
    QDir dir(workingDir);
    if (!dir.exists()) dir.mkpath(".");

    m_fileSystemModel->setRootPath(workingDir);
    ui->treeViewFiles->setModel(m_fileSystemModel);
    ui->treeViewFiles->setRootIndex(m_fileSystemModel->index(workingDir));
    
    // Init Date Edits
    updateChart();
    
    auto dates = importer->datesFromTo();
    if (dates.first.isValid()) {
        ui->dateEditMin->setDate(dates.first.date());
    } else {
        ui->dateEditMin->setDate(QDate::currentDate()); // Default?
    }
    
    if (dates.second.isValid()) {
        ui->dateEditMax->setDate(dates.second.date());
    } else {
        ui->dateEditMax->setDate(QDate::currentDate());
    }

    _refreshImportedFilesList(importer);
}

void PaneOrderFiles::importFile()
{
    QModelIndex index = ui->tableImporters->currentIndex();
    if (!index.isValid()) {
        QMessageBox::warning(this, tr("Import"), tr("Please select an importer first."));
        return;
    }

    AbstractImporterFile *importer = m_importersTable->getImporter(index);
    if (!importer) {
        return;
    }

    QSettings settings;
    QString key = QString("PaneOrderFiles/LastDir/%1").arg(importer->getId());
    QString lastDir = settings.value(key, QDir::homePath()).toString();

    QStringList filePaths = QFileDialog::getOpenFileNames(this, tr("Select Files to Import"),
                                                    lastDir,
                                                    tr("All Files (*.*)")); // Filters could be improved based on importer
    if (filePaths.isEmpty()) {
        return;
    }

    settings.setValue(key, QFileInfo(filePaths.first()).absolutePath());
    filePaths.sort();

    // Run import asynchronously
    // We utilize QCoro to handle the async task
    
    [this](PaneOrderFiles *self, AbstractImporterFile *importer, QStringList paths) -> QCoro::Task<void> {
        // Disable UI?
        self->ui->buttonImport->setEnabled(false);
        
        try {
            // Create callback for missing data - opens DialogVatParams
            auto callbackAddIfMissing = [self](const QString &errorTitle, const QString &errorText) -> QCoro::Task<bool> {
                DialogVatParams dialog(errorTitle, errorText, self);
                int result = dialog.exec();
                co_return (result == QDialog::Accepted);
            };

            // Asked when a file's unique ID was already recorded as imported. Lets the user
            // deliberately re-import it (e.g. an updated report with the same filename) — the
            // new data will replace/complement previously imported data via OrderManager's own
            // conflict handling.
            auto callbackConfirmReimport = [self](const QString &fileName, const QDateTime &previousImportDate) -> QCoro::Task<bool> {
                const QString when = previousImportDate.isValid()
                        ? QLocale().toString(previousImportDate, QLocale::ShortFormat)
                        : self->tr("an earlier session");
                int result = QMessageBox::question(self, self->tr("File Already Imported"),
                        self->tr("\"%1\" was already imported (on %2).\n\n"
                                 "Re-import it now and replace any different data?").arg(fileName, when),
                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                co_return (result == QMessageBox::Yes);
            };

            // Ensure the importer reads fbacenters.csv from the same location as the
            // settings pane (WorkingDirectoryManager's root dir), so centers added via
            // DialogVatParams are visible to the importer on retry.
            importer->setSharedConfigDirectory(WorkingDirectoryManager::instance()->workingDir());
            importer->setWorkingDirectory(QDir(AbstractImporterFile::GET_WORKING_DIR(
                WorkingDirectoryManager::instance()->workingDir(), importer->getId())));

            AbstractImporter::ReturnOrderInfos aggregatedResult;
            aggregatedResult.orderInfos = QSharedPointer<AbstractImporter::OrderInfos>::create();
            QStringList errors;
            QStringList successfulPaths; // tracks files that loaded without error
            // Per-file (path, dateMin, dateMax), used to mark files as imported only once
            // the aggregated import is actually confirmed and committed (see markReportImported()).
            QList<std::tuple<QString, QDate, QDate>> pendingMarkAsImported;

            for (const QString &path : paths) {
                // Load Report with callback
                auto result = co_await importer->loadReport(path, callbackAddIfMissing, callbackConfirmReimport);

                if (!result.errorReturned.isEmpty()) {
                    errors.append(QString("File: %1\nError: %2").arg(QFileInfo(path).fileName(), result.errorReturned));
                    continue;
                }
                successfulPaths.append(path);

                if (result.orderInfos) {
                    pendingMarkAsImported.append({path, result.orderInfos->dateMin, result.orderInfos->dateMax});
                   aggregatedResult.orderInfos->shipments.append(result.orderInfos->shipments);
                   aggregatedResult.orderInfos->refunds.append(result.orderInfos->refunds);
                   aggregatedResult.orderInfos->orderAddresses.append(result.orderInfos->orderAddresses);
                   aggregatedResult.orderInfos->invoicingInfos.append(result.orderInfos->invoicingInfos);
                   
                   // Merge maps
                   for (auto it = result.orderInfos->orderId_refundClues.constBegin();
                        it != result.orderInfos->orderId_refundClues.constEnd(); ++it) {
                       aggregatedResult.orderInfos->orderId_refundClues[it.key()].append(it.value());
                   }
                   aggregatedResult.orderInfos->orderId_infos.insert(result.orderInfos->orderId_infos);

                   // Merge inventory moves (txnIds are unique so inner insert is safe)
                   for (auto it1 = result.orderInfos->year_month_countryFrom_countryTo_eventId_sku_units.constBegin();
                        it1 != result.orderInfos->year_month_countryFrom_countryTo_eventId_sku_units.constEnd(); ++it1) {
                       for (auto it2 = it1.value().constBegin(); it2 != it1.value().constEnd(); ++it2) {
                           for (auto it3 = it2.value().constBegin(); it3 != it2.value().constEnd(); ++it3) {
                               for (auto it4 = it3.value().constBegin(); it4 != it3.value().constEnd(); ++it4) {
                                   aggregatedResult.orderInfos->year_month_countryFrom_countryTo_eventId_sku_units
                                           [it1.key()][it2.key()][it3.key()][it4.key()]
                                           .insert(it4.value());
                               }
                           }
                       }
                   }

                   // Update dates
                   if (aggregatedResult.orderInfos->dateMin.isNull() || (result.orderInfos->dateMin.isValid() && result.orderInfos->dateMin < aggregatedResult.orderInfos->dateMin)) {
                       aggregatedResult.orderInfos->dateMin = result.orderInfos->dateMin;
                   }
                   if (aggregatedResult.orderInfos->dateMax.isNull() || (result.orderInfos->dateMax.isValid() && result.orderInfos->dateMax > aggregatedResult.orderInfos->dateMax)) {
                       aggregatedResult.orderInfos->dateMax = result.orderInfos->dateMax;
                   }
                }
            }
            
            if (!errors.isEmpty()) {
                QMessageBox::warning(self, tr("Import Errors"), errors.join("\n\n"));
            }

            const bool hasShipmentsOrRefunds = !aggregatedResult.orderInfos->shipments.isEmpty()
                                               || !aggregatedResult.orderInfos->refunds.isEmpty();
            const bool hasRefundClues = !aggregatedResult.orderInfos->orderId_refundClues.isEmpty();
            if (hasShipmentsOrRefunds || hasRefundClues) {
                int importedCount = 0;
                QDir workingDir(WorkingDirectoryManager::instance()->workingDir());
                CompanyInfosTable companyInfo(workingDir);

                if (hasShipmentsOrRefunds) {
                    CurrencyRateManager currencyRateManager(workingDir, companyInfo.getApiKeyFixer());

                    if (importer->recomputeTaxes()) {
                        VatTerritoryResolver vatTerritoryResolver(workingDir);
                        TaxResolver taxResolver(workingDir);
                        VatResolver vatResolver(workingDir);

                        QHash<QString, const Address *> orderIdToAddress;
                        for (const auto &addrWithId : aggregatedResult.orderInfos->orderAddresses) {
                            orderIdToAddress[addrWithId.orderId] = &addrWithId.address;
                        }

                        auto computeShipmentTax = [&](Shipment &shipment) {
                            QString vatTerritoryTo;
                            if (!shipment.getActivities().isEmpty()) {
                                const Address *addr = orderIdToAddress.value(
                                            shipment.getActivities().first().getEventId(), nullptr);
                                if (addr) {
                                    vatTerritoryTo = vatTerritoryResolver.getTerritoryId(
                                                addr->getCountryCode(),
                                                addr->getPostalCode(),
                                                addr->getCity());
                                }
                            }
                            shipment.computeTax(&taxResolver, &vatResolver, QString{}, vatTerritoryTo);
                        };

                        for (auto &shipment : aggregatedResult.orderInfos->shipments) {
                            computeShipmentTax(shipment);
                        }
                        for (auto &refund : aggregatedResult.orderInfos->refunds) {
                            computeShipmentTax(refund);
                        }
                    }

                    DialogViewOrders dialog(*aggregatedResult.orderInfos, &currencyRateManager, companyInfo.getCurrency(), workingDir, companyInfo.getCompanyCountryCode(), self);
                    if (dialog.exec() != QDialog::Accepted) {
                        self->ui->buttonImport->setEnabled(true);
                        co_return;
                    }
                }
                setCursor(Qt::WaitCursor);

                OrderManager manager(workingDir);

                // We need to store the value in a local variable because getActivitySource() returns by value (temporary),
                // and we cannot take the address of a temporary.
                ActivitySource source = importer->getActivitySource();

                // Process Shipments & Refunds
                {
                    QList<OrderManager::ShipmentFromSourceEntry> entries;
                    entries.reserve(aggregatedResult.orderInfos->shipments.size()
                                    + aggregatedResult.orderInfos->refunds.size());
                    for (const auto &shipment : aggregatedResult.orderInfos->shipments) {
                        entries.append({shipment.getActivities().first().getEventId(), &shipment, QDate(), importer->isWrongIfConflict(), false});
                    }
                    for (const auto &refund : aggregatedResult.orderInfos->refunds) {
                        entries.append({refund.getActivities().first().getEventId(), &refund, QDate(), importer->isWrongIfConflict(), importer->fixRefundDate()});
                    }
                    manager.recordShipmentsFromSource(&source, entries);
                }

                // Process orderId→store/grouping/customerAccount mapping
                if (!aggregatedResult.orderInfos->orderId_infos.isEmpty()) {
                    manager.recordOrders(aggregatedResult.orderInfos->orderId_infos);
                }

                // Process Addresses
                {
                    QHash<QString, Address> addrMap;
                    for (const auto &addr : aggregatedResult.orderInfos->orderAddresses) {
                        addrMap.insert(addr.orderId, addr.address);
                    }
                    manager.recordAddressesTo(addrMap);
                }

                // Process InvoicingInfos
                for (const auto &inv : aggregatedResult.orderInfos->invoicingInfos) {
                    manager.recordInvoicingInfo(inv.shipmentOrRefundId, &inv.invoicingInfo);
                }

                // Process Inventory Moves
                if (!aggregatedResult.orderInfos->year_month_countryFrom_countryTo_eventId_sku_units.isEmpty()) {
                    manager.recordInventoryMove(
                            aggregatedResult.orderInfos->year_month_countryFrom_countryTo_eventId_sku_units);
                }

                // Process Refund Clues
                QStringList refundErrors;
                for (auto it = aggregatedResult.orderInfos->orderId_refundClues.constBegin();
                     it != aggregatedResult.orderInfos->orderId_refundClues.constEnd(); ++it) {
                    for (const auto &clue : it.value()) {
                        auto callbackPick = [self](const QString &errorTitle,
                                const QString &errorText,
                                const QList<QSharedPointer<Shipment>> &shipmentsToPick) -> QCoro::Task<QString> {
                            DialogPickShipment dialog(errorTitle, errorText, shipmentsToPick, self);
                            if (dialog.exec() == QDialog::Accepted) {
                                co_return dialog.selectedShipmentId();
                            }
                            co_return QString{};
                        };
                        QString err = co_await manager.tryRecordRefund(
                                    it.key(), clue.value, clue.currency, QString{}, clue.date, callbackPick);
                        if (!err.isEmpty()) {
                            refundErrors.append(err);
                        }
                    }
                }
                if (!refundErrors.isEmpty()) {
                    QMessageBox::warning(self, tr("Refund Errors"), refundErrors.join("\n\n"));
                }

                // Only now that everything above has actually been committed to OrderManager
                // do we record the files as imported — a cancelled dialog or an exception
                // thrown during the commit above must NOT mark a file as imported.
                for (const auto &[markPath, markDateMin, markDateMax] : std::as_const(pendingMarkAsImported)) {
                    importer->markReportImported(markPath, markDateMin, markDateMax);
                }

                importedCount = aggregatedResult.orderInfos->countAll();

                // Append one CSV row per imported file to the audit log.
                {
                    const QString logPath = workingDir.absoluteFilePath("import_log.csv");
                    QFile logFile(logPath);
                    const bool writeHeader = !logFile.exists();
                    if (logFile.open(QIODevice::Append | QIODevice::Text)) {
                        QTextStream out(&logFile);
                        out.setEncoding(QStringConverter::Utf8);
                        if (writeHeader) {
                            out << "DateTime;Importer;File;Shipments;Refunds;DateFrom;DateTo\n";
                        }
                        const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
                        const QString label = importer->getLabel();
                        const int ships = aggregatedResult.orderInfos->shipments.size();
                        const int refs  = aggregatedResult.orderInfos->refunds.size();
                        const QString dateFrom = aggregatedResult.orderInfos->dateMin.isValid()
                            ? aggregatedResult.orderInfos->dateMin.toString(Qt::ISODate) : QString{};
                        const QString dateTo = aggregatedResult.orderInfos->dateMax.isValid()
                            ? aggregatedResult.orderInfos->dateMax.toString(Qt::ISODate) : QString{};
                        for (const QString &p : std::as_const(successfulPaths)) {
                            out << now << ";" << label << ";" << QFileInfo(p).fileName()
                                << ";" << ships << ";" << refs
                                << ";" << dateFrom << ";" << dateTo << "\n";
                        }
                    }
                }

                QMetaObject::invokeMethod(self, [self, successfulPaths]() {
                    for (int i = successfulPaths.size() - 1; i >= 0; --i) {
                        self->ui->listImportedFiles->insertItem(0, QFileInfo(successfulPaths[i]).fileName());
                    }
                }, Qt::QueuedConnection);

                // Update Chart Data
                if (importedCount > 0) {
                    auto &counts = self->m_ordersData[importer->getId()];
                    for (const auto &shipment : aggregatedResult.orderInfos->shipments) {
                        // Using first activity date as approximation for shipment date
                        if (!shipment.getActivities().isEmpty()) {
                            counts[shipment.getActivities().first().getDateTime().date()]++;
                        }
                    }
                    for (const auto &refund : aggregatedResult.orderInfos->refunds) {
                        if (!refund.getActivities().isEmpty()) {
                            counts[refund.getActivities().first().getDateTime().date()]++;
                        }
                    }

                    // Trigger chart update on main thread
                    QMetaObject::invokeMethod(self, "updateChart", Qt::QueuedConnection);

                    // Add activities to table on main thread (or safe way)
                    // Since we are in lambda, we can access self.
                    // Thread correctness: The lambda runs in QCoro continuation which might be main thread if started from there?
                    // QCoro::Task usually resumes on the context it started. importFile called from UI thread.
                    // However, let's be safe or just do it.

                    QList<Activity> newActivities;
                    for (const auto &shipment : aggregatedResult.orderInfos->shipments) {
                        newActivities.append(shipment.getActivities());
                    }
                    for (const auto &refund : aggregatedResult.orderInfos->refunds) {
                        newActivities.append(refund.getActivities());
                    }

                    if (!newActivities.isEmpty()) {
                        // Using invokeMethod to ensure we modify model on main thread if not already
                        QMetaObject::invokeMethod(self, [self, importerId = importer->getId(), newActivities]() {
                            if (self->m_activityModels.contains(importerId)) {
                                self->m_activityModels[importerId]->addActivities(newActivities);
                            } else {
                                // Should have been created in onImporterSelected, but if not:
                                auto *table = new ActivityTable(self);
                                table->addActivities(newActivities);
                                self->m_activityModels[importerId] = table;
                                // If this importer is currently selected, update view
                                // We checked onImporterSelected for validity, but let's see.
                                // Simple check:
                                QModelIndex index = self->ui->tableImporters->currentIndex();
                                if (index.isValid()) {
                                    auto *currentImporter = self->m_importersTable->getImporter(index);
                                    if (currentImporter && currentImporter->getId() == importerId) {
                                        self->ui->tableOrders->setModel(table);
                                    }
                                }
                            }
                        }, Qt::QueuedConnection);
                    }
                }
                
                setCursor(Qt::ArrowCursor);
                QMessageBox::information(self, tr("Import Successful"),
                                         tr("Successfully imported %1 items.").arg(importedCount));
            } else if (errors.isEmpty()) {
                 // Nothing to commit, so nothing to cancel/fail — safe to mark the parsed
                 // files as imported right away, so they aren't needlessly re-parsed later.
                 for (const auto &[markPath, markDateMin, markDateMax] : std::as_const(pendingMarkAsImported)) {
                     importer->markReportImported(markPath, markDateMin, markDateMax);
                 }
                 QMessageBox::information(self, tr("Import"), tr("No data found to import."));
            }
            
        } catch (const CsvHeaderException &e) {
             QMessageBox::critical(self, tr("Columns error"), e.getErrorColumns(tr("CSV missing columns:")));
        } catch (const ExceptionWithTitleText &e) {
             QMessageBox::critical(self, e.errorTitle(), e.errorText());
        } catch (const QException &e) {
             QMessageBox::critical(self, tr("Import Error"), e.what());
        } catch (const std::exception &e) {
             QMessageBox::critical(self, tr("Import Error"), QString(e.what()));
        }

        self->ui->buttonImport->setEnabled(true);
        
    }(this, importer, filePaths);
}

void PaneOrderFiles::removeFile()
{
    QModelIndex index = ui->treeViewFiles->currentIndex();
    if (!index.isValid()) {
        QMessageBox::warning(this, tr("Remove File"), tr("Please select a file to remove."));
        return;
    }

    // Check if it's a file, not a directory (unless we want to support recursive delete?)
    // Requirements say "remove selected file" usually.
    if (m_fileSystemModel->isDir(index)) {
        QMessageBox::warning(this, tr("Remove File"), tr("Please select a file, not a directory."));
        return;
    }
    
    QString fileName = m_fileSystemModel->fileName(index);
    
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, tr("Remove File"), 
                                  tr("Are you sure you want to delete '%1'?").arg(fileName),
                                  QMessageBox::Yes|QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        if (!m_fileSystemModel->remove(index)) {
            QMessageBox::critical(this, tr("Remove Error"), tr("Could not delete file: %1").arg(fileName));
        }
    }
}

void PaneOrderFiles::updateChart()
{
    // Clear previous chart
    ui->chartOrders->setChart(new QChart());
    
    QModelIndex index = ui->tableImporters->currentIndex();
    if (!index.isValid()) return;
    
    AbstractImporterFile *importer = m_importersTable->getImporter(index);
    if (!importer) return;
    
    const auto &counts = m_ordersData[importer->getId()];
    if (counts.isEmpty()) return;
    
    auto *series = new QLineSeries();
    series->setName(tr("Orders"));
    
    int maxCount = 0;
    
    // Sort keys just in case (QMap is sorted by key)
    for (auto it = counts.begin(); it != counts.end(); ++it) {
        // Convert date to milliseconds for QDateTimeAxis
        series->append(it.key().startOfDay().toMSecsSinceEpoch(), it.value());
        if (it.value() > maxCount) maxCount = it.value();
    }
    
    auto *chart = new QChart();
    chart->addSeries(series);
    chart->legend()->hide();
    chart->setTitle(tr("Imported Orders Over Time"));
    
    auto *axisX = new QDateTimeAxis();
    axisX->setTickCount(10);
    axisX->setFormat("dd MMM yyyy");
    axisX->setTitleText("Date");
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);
    
    auto *axisY = new QValueAxis();
    axisY->setLabelFormat("%i");
    axisY->setTitleText("Count");
    axisY->setMin(0);
    axisY->setMax(maxCount + 1); // Add some padding
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);
    
    ui->chartOrders->setChart(chart);
    ui->chartOrders->setRenderHint(QPainter::Antialiasing);
}
