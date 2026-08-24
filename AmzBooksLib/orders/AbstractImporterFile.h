#ifndef ABSTRACTIMPORTERFILE_H
#define ABSTRACTIMPORTERFILE_H

#include <QCoroTask>
#include <QDir>
#include <QDate>
#include <QDateTime>
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

    // Parses filePath only. Does NOT record the file as imported — call markReportImported()
    // once the caller has actually committed the resulting OrderInfos, otherwise a
    // cancelled/failed import would still block re-importing the same file later.
    // If the file was already recorded as imported, callbackConfirmReimport (fileName, previousImportDate)
    // is awaited to ask whether to re-parse and let the caller replace the previously imported data;
    // if it's null or returns false, the call fails with "Report already imported".
    QCoro::Task<ReturnOrderInfos> loadReport(
        const QString &filePath,
        std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing = nullptr,
        std::function<QCoro::Task<bool>(const QString &fileName, const QDateTime &previousImportDate)> callbackConfirmReimport = nullptr);

    // Records filePath as imported: copies it under reports/<label>/<year of dateMin>/,
    // registers its unique ID (idempotent — safe to call again for a re-imported file),
    // and extends the importer's known ImportedFrom/ImportedTo range.
    // Call only after the caller has actually committed the corresponding data.
    void markReportImported(const QString &filePath, const QDate &dateMin, const QDate &dateMax);

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
