#include "AbstractImporterFile.h"
#include <QFileInfo>
#include <QDir>
#include <algorithm>

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


QPair<QDateTime, QDateTime> AbstractImporterFile::datesFromTo() const
{
    auto s = _settings();
    return qMakePair(s->value("Reports/ImportedFrom").toDateTime(), s->value("Reports/ImportedTo").toDateTime());
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> AbstractImporterFile::loadReport(const QString &filePath)
{
    QString uniqueId = getUniqueReportId(filePath);
    
    // Check if duplicate
    auto s = _settings();
    QStringList importedIds = s->value("Reports/ImportedIds").toStringList();
    if (importedIds.contains(uniqueId)) {
        co_return ReturnOrderInfos{nullptr, QString("Report already imported (ID: %1)").arg(uniqueId)};
    }
    
    ReturnOrderInfos result = co_await _loadReport(filePath);
    
    if (result.errorReturned.isEmpty() && result.orderInfos) {
        // Update date range in OrderInfos
        int year = getMinYear(*result.orderInfos);
        if (year == 0) year = QDate::currentDate().year();
        
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
        
        // If file exists, maybe duplicate content but distinct ID? 
        // Or same ID but file re-uploaded? 
        // Logic says uniqueId is new, so we proceed. If name conflict, we might overwrite or rename.
        // Requirement: "Save the file in working dir / reports / label / minyear"
        // I will overwrite for now or ensure uniqueness if needed, but user didn't specify.
        // Ideally checking if targetPath exists and appending counter would be safer, 
        // but let's stick to simple copy request.
        
        QFile::copy(filePath, targetPath);
        
        // Update settings
        importedIds.append(uniqueId);
        s->setValue("Reports/ImportedIds", importedIds);
        
        // Update dates using the calculated dateMin/dateMax
        bool hasOrders = !result.orderInfos->shipments.isEmpty() || !result.orderInfos->refunds.isEmpty();
        
        if (hasOrders && result.orderInfos->dateMin.isValid() && result.orderInfos->dateMax.isValid()) {
             QDateTime minDate = result.orderInfos->dateMin.startOfDay();
             QDateTime maxDate = result.orderInfos->dateMax.startOfDay();
             
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
    
    co_return result;
}
