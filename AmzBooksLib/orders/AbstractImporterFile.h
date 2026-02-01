#ifndef ABSTRACTIMPORTERFILE_H
#define ABSTRACTIMPORTERFILE_H

#include <QCoroTask>
#include <QDir>
#include <functional>

#include "AbstractImporter.h"


class AbstractImporterFile : public AbstractImporter
{
public:
    static const QString FOLDER_NAME;
    static QString GET_WORKING_DIR(const QDir &workingDir, const QString &importerId);
    static QString GET_WORKING_DIR(const QDir &workingDir, const QString &importerId, const QString &yearDir);
    static const QMap<QString, const AbstractImporterFile *> &ALL_IMPORTERS();
    using AbstractImporter::AbstractImporter; // Inherit constructor

    QPair<QDateTime, QDateTime> datesFromTo() const; 
    
    QCoro::Task<ReturnOrderInfos> loadReport(
        const QString &filePath,
        std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing = nullptr);

    static QDate parseDateFormats(const QString &dateStr, const QStringList &formats);

    virtual QString getUniqueReportId(const QString &filePath) const = 0;
    class Recorder{
    public:
        Recorder(AbstractImporterFile *dataGetter);
    };

protected:
    virtual QCoro::Task<ReturnOrderInfos> _loadReport(
        const QString &filePath,
        std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing) = 0;
    static QMap<QString, const AbstractImporterFile *> &getImporters();
};

#define DECLARE_IMPORTER_FILE(NEW_CLASS) \
NEW_CLASS instance##NEW_CLASS; \
    AbstractImporterFile::Recorder recorder##NEW_CLASS{&instance##NEW_CLASS};

#endif // ABSTRACTIMPORTERFILE_H
