// Headless report import tool.
//
// Replicates the import flow of PaneOrderFiles (load report → optional tax
// recompute → OrderManager recording → refund clues) without any GUI dialog:
//   - missing config (FBA center, VAT rate…) fails the file instead of
//     prompting;
//   - ambiguous refund matches are skipped and reported instead of opening
//     DialogPickShipment;
//   - the DialogViewOrders confirmation step is skipped (data is recorded
//     directly) — intended for scripted imports against a staging copy of
//     the working directory.
//
// Usage:
//   AmzBooksImportCli --working-dir <dir> --importer <label> <file> [file…]
//   AmzBooksImportCli --list-importers

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QTextStream>
#include <QHash>

#include <QCoroTask>

#include "orders/AbstractImporterFile.h"
#include "orders/OrderManager.h"
#include "books/CompanyInfosTable.h"
#include "books/TaxResolver.h"
#include "books/VatResolver.h"
#include "books/VatTerritoryResolver.h"
#include "CurrencyRateManager.h"
#include "ExceptionWithTitleText.h"

namespace {

struct ImportStats {
    int shipments = 0;
    int refunds = 0;
    int refundCluesOk = 0;
    QStringList fileErrors;
    QStringList refundErrors;
};

QCoro::Task<ImportStats> runImport(AbstractImporterFile *importer,
                                   const QDir &workingDir,
                                   const QStringList &paths)
{
    ImportStats stats;
    QTextStream out(stdout);

    auto callbackAddIfMissing = [&out](const QString &errorTitle,
                                       const QString &errorText) -> QCoro::Task<bool> {
        out << "  [missing-config] " << errorTitle << ": " << errorText << "\n";
        out.flush();
        co_return false; // headless: never retry, fail the file
    };

    importer->setSharedConfigDirectory(workingDir);
    importer->setWorkingDirectory(QDir(AbstractImporterFile::GET_WORKING_DIR(
        workingDir, importer->getId())));
    importer->load();

    AbstractImporter::ReturnOrderInfos aggregatedResult;
    aggregatedResult.orderInfos = QSharedPointer<AbstractImporter::OrderInfos>::create();
    QStringList successfulPaths;
    // Per-file (path, dateMin, dateMax); files are only marked as imported once the
    // import below is actually committed to OrderManager (see markReportImported()).
    QList<std::tuple<QString, QDate, QDate>> pendingMarkAsImported;

    // Headless: never ask whether to re-import an already-recorded file; treat it as an error.
    for (const QString &path : paths) {
        out << "Loading " << path << "…\n";
        out.flush();
        auto result = co_await importer->loadReport(path, callbackAddIfMissing);
        if (!result.errorReturned.isEmpty()) {
            stats.fileErrors.append(QString("%1: %2").arg(QFileInfo(path).fileName(),
                                                          result.errorReturned));
            continue;
        }
        successfulPaths.append(path);
        if (!result.orderInfos) {
            continue;
        }
        pendingMarkAsImported.append({path, result.orderInfos->dateMin, result.orderInfos->dateMax});

        aggregatedResult.orderInfos->shipments.append(result.orderInfos->shipments);
        aggregatedResult.orderInfos->refunds.append(result.orderInfos->refunds);
        aggregatedResult.orderInfos->orderAddresses.append(result.orderInfos->orderAddresses);
        aggregatedResult.orderInfos->invoicingInfos.append(result.orderInfos->invoicingInfos);
        for (auto it = result.orderInfos->orderId_refundClues.constBegin();
             it != result.orderInfos->orderId_refundClues.constEnd(); ++it) {
            aggregatedResult.orderInfos->orderId_refundClues[it.key()].append(it.value());
        }
        aggregatedResult.orderInfos->orderId_infos.insert(result.orderInfos->orderId_infos);
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
        if (aggregatedResult.orderInfos->dateMin.isNull()
            || (result.orderInfos->dateMin.isValid()
                && result.orderInfos->dateMin < aggregatedResult.orderInfos->dateMin)) {
            aggregatedResult.orderInfos->dateMin = result.orderInfos->dateMin;
        }
        if (aggregatedResult.orderInfos->dateMax.isNull()
            || (result.orderInfos->dateMax.isValid()
                && result.orderInfos->dateMax > aggregatedResult.orderInfos->dateMax)) {
            aggregatedResult.orderInfos->dateMax = result.orderInfos->dateMax;
        }
    }

    const bool hasShipmentsOrRefunds = !aggregatedResult.orderInfos->shipments.isEmpty()
                                       || !aggregatedResult.orderInfos->refunds.isEmpty();
    const bool hasRefundClues = !aggregatedResult.orderInfos->orderId_refundClues.isEmpty();

    if (!hasShipmentsOrRefunds && !hasRefundClues) {
        out << "Nothing to record.\n";
        out.flush();
        for (const auto &[markPath, markDateMin, markDateMax] : std::as_const(pendingMarkAsImported)) {
            importer->markReportImported(markPath, markDateMin, markDateMax);
        }
        co_return stats;
    }

    CompanyInfosTable companyInfo(workingDir);

    if (hasShipmentsOrRefunds && importer->recomputeTaxes()) {
        VatTerritoryResolver vatTerritoryResolver(workingDir);
        TaxResolver taxResolver(workingDir);
        VatResolver vatResolver(workingDir);

        QHash<QString, const Address *> orderIdToAddress;
        for (const auto &addrWithId : std::as_const(aggregatedResult.orderInfos->orderAddresses)) {
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

    OrderManager manager(workingDir);
    ActivitySource source = importer->getActivitySource();

    {
        QList<OrderManager::ShipmentFromSourceEntry> entries;
        entries.reserve(aggregatedResult.orderInfos->shipments.size()
                        + aggregatedResult.orderInfos->refunds.size());
        for (const auto &shipment : std::as_const(aggregatedResult.orderInfos->shipments)) {
            entries.append({shipment.getActivities().first().getEventId(), &shipment,
                            QDate(), importer->isWrongIfConflict(), false});
        }
        for (const auto &refund : std::as_const(aggregatedResult.orderInfos->refunds)) {
            entries.append({refund.getActivities().first().getEventId(), &refund,
                            QDate(), importer->isWrongIfConflict(), importer->fixRefundDate()});
        }
        manager.recordShipmentsFromSource(&source, entries);
    }
    stats.shipments = aggregatedResult.orderInfos->shipments.size();
    stats.refunds = aggregatedResult.orderInfos->refunds.size();

    if (!aggregatedResult.orderInfos->orderId_infos.isEmpty()) {
        manager.recordOrders(aggregatedResult.orderInfos->orderId_infos);
    }

    {
        QHash<QString, Address> addrMap;
        for (const auto &addr : std::as_const(aggregatedResult.orderInfos->orderAddresses)) {
            addrMap.insert(addr.orderId, addr.address);
        }
        manager.recordAddressesTo(addrMap);
    }

    for (const auto &inv : std::as_const(aggregatedResult.orderInfos->invoicingInfos)) {
        manager.recordInvoicingInfo(inv.shipmentOrRefundId, &inv.invoicingInfo);
    }

    if (!aggregatedResult.orderInfos->year_month_countryFrom_countryTo_eventId_sku_units.isEmpty()) {
        manager.recordInventoryMove(
                aggregatedResult.orderInfos->year_month_countryFrom_countryTo_eventId_sku_units);
    }

    // Refund clues — headless: ambiguous matches are skipped, not picked.
    auto callbackPick = [&out](const QString &errorTitle,
                               const QString &errorText,
                               const QList<QSharedPointer<Shipment>> &shipmentsToPick) -> QCoro::Task<QString> {
        out << "  [refund-ambiguous] " << errorTitle << ": " << errorText
            << " (" << shipmentsToPick.size() << " candidates)\n";
        out.flush();
        co_return QString{};
    };
    for (auto it = aggregatedResult.orderInfos->orderId_refundClues.constBegin();
         it != aggregatedResult.orderInfos->orderId_refundClues.constEnd(); ++it) {
        for (const auto &clue : it.value()) {
            QString err = co_await manager.tryRecordRefund(
                        it.key(), clue.value, clue.currency, QString{}, clue.date, callbackPick);
            if (!err.isEmpty()) {
                stats.refundErrors.append(QString("%1: %2").arg(it.key(), err));
            } else {
                ++stats.refundCluesOk;
            }
        }
    }

    // Everything above committed successfully — now record the files as imported.
    for (const auto &[markPath, markDateMin, markDateMax] : std::as_const(pendingMarkAsImported)) {
        importer->markReportImported(markPath, markDateMin, markDateMax);
    }

    // Audit log, same format as the GUI import.
    {
        QFile logFile(workingDir.absoluteFilePath("import_log.csv"));
        const bool writeHeader = !logFile.exists();
        if (logFile.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream logOut(&logFile);
            logOut.setEncoding(QStringConverter::Utf8);
            if (writeHeader) {
                logOut << "DateTime;Importer;File;Shipments;Refunds;DateFrom;DateTo\n";
            }
            const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
            const QString dateFrom = aggregatedResult.orderInfos->dateMin.isValid()
                ? aggregatedResult.orderInfos->dateMin.toString(Qt::ISODate) : QString{};
            const QString dateTo = aggregatedResult.orderInfos->dateMax.isValid()
                ? aggregatedResult.orderInfos->dateMax.toString(Qt::ISODate) : QString{};
            for (const QString &p : std::as_const(successfulPaths)) {
                logOut << now << ";" << importer->getLabel() << ";" << QFileInfo(p).fileName()
                       << ";" << stats.shipments << ";" << stats.refunds
                       << ";" << dateFrom << ";" << dateTo << "\n";
            }
        }
    }

    co_return stats;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("AmzBooksImportCli");

    QCommandLineParser parser;
    parser.setApplicationDescription("Headless AmzBooks report import (no dialogs)");
    parser.addHelpOption();
    parser.addOption({"working-dir", "AmzBooks working directory", "dir"});
    parser.addOption({"importer", "Importer label (see --list-importers)", "label"});
    parser.addOption({"list-importers", "List available file importer labels"});
    parser.addPositionalArgument("files", "Report files to import", "[files…]");
    parser.process(app);

    QTextStream out(stdout);
    const auto &importers = AbstractImporterFile::ALL_IMPORTERS();

    if (parser.isSet("list-importers")) {
        for (auto it = importers.cbegin(); it != importers.cend(); ++it) {
            out << it.key() << "\n";
        }
        return 0;
    }

    const QString workingDirPath = parser.value("working-dir");
    const QString importerLabel = parser.value("importer");
    const QStringList files = parser.positionalArguments();

    if (workingDirPath.isEmpty() || importerLabel.isEmpty() || files.isEmpty()) {
        out << "Missing --working-dir, --importer or files. See --help.\n";
        return 1;
    }
    QDir workingDir(workingDirPath);
    if (!workingDir.exists()) {
        out << "Working directory does not exist: " << workingDirPath << "\n";
        return 1;
    }
    if (!importers.contains(importerLabel)) {
        out << "Unknown importer '" << importerLabel << "'. Available:\n";
        for (auto it = importers.cbegin(); it != importers.cend(); ++it) {
            out << "  " << it.key() << "\n";
        }
        return 1;
    }
    for (const QString &f : files) {
        if (!QFileInfo::exists(f)) {
            out << "File not found: " << f << "\n";
            return 1;
        }
    }

    auto *importer = const_cast<AbstractImporterFile *>(importers.value(importerLabel));

    int exitCode = 0;
    try {
        const ImportStats stats = QCoro::waitFor(runImport(importer, workingDir, files));
        out << "\n=== Import summary ===\n";
        out << "Shipments recorded:    " << stats.shipments << "\n";
        out << "Refunds recorded:      " << stats.refunds << "\n";
        out << "Refund clues applied:  " << stats.refundCluesOk << "\n";
        if (!stats.fileErrors.isEmpty()) {
            out << "File errors (" << stats.fileErrors.size() << "):\n";
            for (const QString &e : std::as_const(stats.fileErrors)) {
                out << "  - " << e << "\n";
            }
            exitCode = 2;
        }
        if (!stats.refundErrors.isEmpty()) {
            out << "Refund errors (" << stats.refundErrors.size() << "):\n";
            for (const QString &e : std::as_const(stats.refundErrors)) {
                out << "  - " << e << "\n";
            }
            exitCode = 2;
        }
    } catch (const ExceptionWithTitleText &e) {
        out << "FATAL: " << e.errorTitle() << ": " << e.errorText() << "\n";
        exitCode = 1;
    } catch (const std::exception &e) {
        out << "FATAL: " << e.what() << "\n";
        exitCode = 1;
    }
    out.flush();
    return exitCode;
}
