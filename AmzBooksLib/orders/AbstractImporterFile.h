#ifndef ABSTRACTIMPORTERFILE_H
#define ABSTRACTIMPORTERFILE_H

#include "AbstractImporter.h"
#include <QCoroTask>


class AbstractImporterFile : public AbstractImporter
{
public:
    static const QMap<QString, const AbstractImporterFile *> &ALL_IMPORTERS();
    using AbstractImporter::AbstractImporter; // Inherit constructor

    QPair<QDateTime, QDateTime> datesFromTo() const; 
    
    QCoro::Task<ReturnOrderInfos> loadReport(const QString &filePath);

    static QDate parseDateFormats(const QString &dateStr, const QStringList &formats);

    virtual QString getUniqueReportId(const QString &filePath) const = 0;
    class Recorder{
    public:
        Recorder(const AbstractImporterFile *dataGetter);
    };

protected:
    virtual QCoro::Task<ReturnOrderInfos> _loadReport(const QString &filePath) = 0;
    static QMap<QString, const AbstractImporterFile *> _IMPORTERS;
};

#define DECLARE_CLASS(NEW_CLASS) \
NEW_CLASS instance##NEW_CLASS; \
    NEW_CLASS::Recorder recorder##NEW_CLASS{&instance##NEW_CLASS};

#endif // ABSTRACTIMPORTERFILE_H
