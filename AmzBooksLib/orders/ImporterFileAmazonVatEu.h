#ifndef IMPORTERFILEAMAZONVATEU_H
#define IMPORTERFILEAMAZONVATEU_H

#include "AbstractImporterFile.h"

class ImporterFileAmazonVatEu : public AbstractImporterFile
{
public:
    using AbstractImporterFile::AbstractImporterFile;

    QString getLabel() const override;
    ActivitySource getActivitySource() const override;
    QString getId() const override;
    QMap<QString, ParamInfo> getRequiredParams() const override;
    
    QString getUniqueReportId(const QString &filePath) const override;
    bool recomputeTaxes() const override;

protected:
    QCoro::Task<ReturnOrderInfos> _loadReport(
        const QString &filePath,
        std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing) override;
};

#endif // IMPORTERFILEAMAZONVATEU_H
