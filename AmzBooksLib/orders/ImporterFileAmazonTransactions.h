#ifndef IMPORTERFILEAMAZONTRANSACTIONS_H
#define IMPORTERFILEAMAZONTRANSACTIONS_H

#include "AbstractImporterFile.h"

class ImporterFileAmazonTransactions : public AbstractImporterFile
{
public:
    using AbstractImporterFile::AbstractImporterFile;
    ImporterFileAmazonTransactions() : AbstractImporterFile() {}

    QString getLabel() const override;
    ActivitySource getActivitySource() const override;
    QMap<QString, ParamInfo> getRequiredParams() const override;

    QString getUniqueReportId(const QString &filePath) const override;

protected:
    QCoro::Task<ReturnOrderInfos> _loadReport(const QString &filePath) override;
};

#endif // IMPORTERFILEAMAZONTRANSACTIONS_H
