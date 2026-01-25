#ifndef ABSTRACTIMPORTERFILE_H
#define ABSTRACTIMPORTERFILE_H

#include "AbstractImporter.h"
#include <QCoroTask>

class AbstractImporterFile : public AbstractImporter
{
public:
    using AbstractImporter::AbstractImporter; // Inherit constructor

    QPair<QDateTime, QDateTime> datesFromTo() const; 
    
    QCoro::Task<ReturnOrderInfos> loadReport(const QString &filePath);

    virtual QString getUniqueReportId(const QString &filePath) const = 0;

protected:
    virtual QCoro::Task<ReturnOrderInfos> _loadReport(const QString &filePath) = 0;
};

#endif // ABSTRACTIMPORTERFILE_H
