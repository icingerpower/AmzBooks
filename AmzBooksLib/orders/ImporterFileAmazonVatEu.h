#ifndef IMPORTERFILEAMAZONVATEU_H
#define IMPORTERFILEAMAZONVATEU_H

#include "AbstractImporterFile.h"

class ImporterFileAmazonVatEu : public AbstractImporterFile
{
public:
    using AbstractImporterFile::AbstractImporterFile;

    QString getLabel() const override;
    ActivitySource getActivitySource() const override;
    QMap<QString, ParamInfo> getRequiredParams() const override;
    
    QString getUniqueReportId(const QString &filePath) const override;

protected:
    QCoro::Task<ReturnOrderInfos> _loadReport(const QString &filePath) override;
};

#endif // IMPORTERFILEAMAZONVATEU_H
