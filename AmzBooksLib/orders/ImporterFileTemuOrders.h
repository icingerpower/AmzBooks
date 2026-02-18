#ifndef IMPORTERFILETEMUORDERS_H
#define IMPORTERFILETEMUORDERS_H

#include "AbstractImporterFile.h"

class ImporterFileTemuOrders : public AbstractImporterFile
{
public:
    using AbstractImporterFile::AbstractImporterFile;
    ImporterFileTemuOrders() : AbstractImporterFile() {}

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

private:
    static double parseEuropeanPrice(const QString &priceStr);
    static QDateTime parseTemuDate(const QString &dateStr);
};

#endif // IMPORTERFILETEMUORDERS_H
