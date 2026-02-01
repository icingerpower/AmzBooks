#ifndef IMPORTERFILEAMAZONFBAINVOICING_H
#define IMPORTERFILEAMAZONFBAINVOICING_H

#include "AbstractImporterFile.h"

class ImporterFileAmazonFbaInvoicing : public AbstractImporterFile
{
public:
    using AbstractImporterFile::AbstractImporterFile;
    ImporterFileAmazonFbaInvoicing() : AbstractImporterFile() {}

    QString getLabel() const override;
    ActivitySource getActivitySource() const override;
    QMap<QString, ParamInfo> getRequiredParams() const override;

    QString getUniqueReportId(const QString &filePath) const override;

protected:
    QCoro::Task<ReturnOrderInfos> _loadReport(const QString &filePath) override;
};

#endif // IMPORTERFILEAMAZONFBAINVOICING_H
