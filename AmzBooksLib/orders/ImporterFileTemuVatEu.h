#ifndef IMPORTERFILETEMUVATEU_H
#define IMPORTERFILETEMUVATEU_H

#include "AbstractImporterFile.h"

class ImporterFileTemuVatEu : public AbstractImporterFile
{
public:
    using AbstractImporterFile::AbstractImporterFile;
    ImporterFileTemuVatEu() : AbstractImporterFile() {}

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

private:
    static double parseEuropeanAmount(const QString &amountStr);
    static QDate parseTemuVatDate(const QString &dateStr);
};

#endif // IMPORTERFILETEMUVATEU_H
