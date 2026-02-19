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
    QString getId() const override;
    QMap<QString, ParamInfo> getRequiredParams() const override;

    QString getUniqueReportId(const QString &filePath) const override;
    bool recomputeTaxes() const override;
    bool isWrongIfConflict() const override;
    bool fixRefundDate() const override;

protected:
    QCoro::Task<ReturnOrderInfos> _loadReport(
        const QString &filePath,
        std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing) override;
};

#endif // IMPORTERFILEAMAZONTRANSACTIONS_H
