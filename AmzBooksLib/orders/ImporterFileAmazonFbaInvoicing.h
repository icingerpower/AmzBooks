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
    QString getId() const override;
    QMap<QString, ParamInfo> getRequiredParams() const override;

    QString getUniqueReportId(const QString &filePath) const override;
    bool recomputeTaxes() const override;
    bool isWrongIfConflict() const override;

protected:
    QCoro::Task<ReturnOrderInfos> _loadReport(
        const QString &filePath,
        std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing) override;
};

#endif // IMPORTERFILEAMAZONFBAINVOICING_H
