#include "AbstractImporterFile.h"
#include <QFileInfo>
#include <QDir>
#include <algorithm>

// QMap<QString, const AbstractImporterFile *> AbstractImporterFile::_IMPORTERS;

const QString AbstractImporterFile::FOLDER_NAME{"sale_reports"}; // Don't translate

QMap<QString, const AbstractImporterFile *> &AbstractImporterFile::getImporters() {
    static QMap<QString, const AbstractImporterFile *> importers;
    return importers;
}

// Helper to find min year from OrderInfos and update date range
static int getMinYear(AbstractImporter::OrderInfos& infos) {
    int minYear = 0;
    QDateTime minDateTime;
    QDateTime maxDateTime;
    
    auto updateDateRange = [&minDateTime, &maxDateTime, &minYear](const QDateTime &dt) {
        if (dt.isValid()) {
            int y = dt.date().year();
            if (minYear == 0 || y < minYear) minYear = y;
            if (!minDateTime.isValid() || dt < minDateTime) minDateTime = dt;
            if (!maxDateTime.isValid() || dt > maxDateTime) maxDateTime = dt;
        }
    };

    for (const auto& shipment : infos.shipments) {
        for (const auto& activity : shipment.getActivities()) {
             updateDateRange(activity.getDateTime());
        }
    }
    for (const auto& refund : infos.refunds) {
        for (const auto& activity : refund.getActivities()) {
             updateDateRange(activity.getDateTime());
        }
    }
    
    if (minDateTime.isValid()) infos.dateMin = minDateTime.date();
    if (maxDateTime.isValid()) infos.dateMax = maxDateTime.date();
    
    return minYear;
}

QString AbstractImporterFile::GET_WORKING_DIR(
        const QDir &workingDir, const QString &importerId)
{
    return QDir{workingDir.absoluteFilePath(FOLDER_NAME)}.absoluteFilePath(importerId);
}

const QMap<QString, const AbstractImporterFile *> &AbstractImporterFile::ALL_IMPORTERS()
{
    return getImporters();
}

QPair<QDateTime, QDateTime> AbstractImporterFile::datesFromTo() const
{
    auto s = _settings();
    return qMakePair(s->value("Reports/ImportedFrom").toDateTime(), s->value("Reports/ImportedTo").toDateTime());
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> AbstractImporterFile::loadReport(
    const QString &filePath,
    std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing,
    std::function<QCoro::Task<bool>(const QString &fileName, const QDateTime &previousImportDate)> callbackConfirmReimport)
{
    QString uniqueId = getUniqueReportId(filePath);

    // Check if duplicate
    auto s = _settings();
    QStringList importedIds = s->value("Reports/ImportedIds").toStringList();
    if (importedIds.contains(uniqueId)) {
        bool reimport = false;
        if (callbackConfirmReimport) {
            QDateTime previousImportDate = s->value("Reports/ImportedAt/" + uniqueId).toDateTime();
            reimport = co_await callbackConfirmReimport(QFileInfo(filePath).fileName(), previousImportDate);
        }
        if (!reimport) {
            co_return ReturnOrderInfos{nullptr, QString("Report already imported (ID: %1)").arg(uniqueId)};
        }
    }

    ReturnOrderInfos result = co_await _loadReport(filePath, callbackAddIfMissing);

    if (result.errorReturned.isEmpty() && result.orderInfos) {
        auto &shipments = result.orderInfos->shipments;
        for (int i=shipments.size()-1; i>-1; --i) {
            if (qAbs(shipments[i].getTotalTaxed()) < 0.00001) {
                shipments.removeAt(i);
            }
        }
        auto &refunds = result.orderInfos->refunds;
        for (int i=refunds.size()-1; i>-1; --i) {
            if (qAbs(refunds[i].getTotalTaxed()) < 0.00001) {
                refunds.removeAt(i);
            }
        }
        // Update date range in OrderInfos (used by the caller for aggregation/display;
        // does NOT record the file as imported — see markReportImported()).
        getMinYear(*result.orderInfos);
    }

    co_return result;
}

void AbstractImporterFile::markReportImported(const QString &filePath, const QDate &dateMin, const QDate &dateMax)
{
    QString uniqueId = getUniqueReportId(filePath);
    int year = dateMin.isValid() ? dateMin.year() : QDate::currentDate().year();

    QDir reportDir = m_workingDirectory;
    if (!reportDir.exists("reports")) reportDir.mkdir("reports");
    reportDir.cd("reports");

    QString labelSafe = getLabel().simplified().replace(" ", "_"); // Basic sanitization
    if (!reportDir.exists(labelSafe)) reportDir.mkdir(labelSafe);
    reportDir.cd(labelSafe);

    QString yearStr = QString::number(year);
    if (!reportDir.exists(yearStr)) reportDir.mkdir(yearStr);
    reportDir.cd(yearStr);

    QString targetPath = reportDir.absoluteFilePath(QFileInfo(filePath).fileName());
    if (QFile::exists(targetPath)) {
        // Re-import of a file already recorded: refresh the saved copy with the new content.
        QFile::remove(targetPath);
    }
    QFile::copy(filePath, targetPath);

    auto s = _settings();
    QStringList importedIds = s->value("Reports/ImportedIds").toStringList();
    if (!importedIds.contains(uniqueId)) {
        importedIds.append(uniqueId);
        s->setValue("Reports/ImportedIds", importedIds);
    }
    s->setValue("Reports/ImportedAt/" + uniqueId, QDateTime::currentDateTime());

    if (dateMin.isValid() && dateMax.isValid()) {
        QDateTime minDate = dateMin.startOfDay();
        QDateTime maxDate = dateMax.startOfDay();

        QDateTime currentFrom = s->value("Reports/ImportedFrom").toDateTime();
        if (!currentFrom.isValid() || minDate < currentFrom) {
            s->setValue("Reports/ImportedFrom", minDate);
        }

        QDateTime currentTo = s->value("Reports/ImportedTo").toDateTime();
        if (!currentTo.isValid() || maxDate > currentTo) {
            s->setValue("Reports/ImportedTo", maxDate);
        }
    }
}

QDate AbstractImporterFile::parseDateFormats(const QString &dateStr, const QStringList &formats)
{
    for (const QString &format : formats) {
        QDate date = QDate::fromString(dateStr, format);
        if (date.isValid()) {
            return date;
        }
    }
    return QDate();
}

AbstractImporterFile::Recorder::Recorder(AbstractImporterFile *dataGetter)
{
    dataGetter->load();
    getImporters()[dataGetter->getLabel()] = dataGetter;
}
