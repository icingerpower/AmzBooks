#ifndef IMPORTERFILECOMMERCEHQ_H
#define IMPORTERFILECOMMERCEHQ_H

#include "AbstractImporterFile.h"

class ImporterFileCommerceHQ : public AbstractImporterFile
{
public:
    using AbstractImporterFile::AbstractImporterFile;
    ImporterFileCommerceHQ() : AbstractImporterFile() {}

    QString getLabel() const override;
    ActivitySource getActivitySource() const override;
    QString getId() const override;
    QMap<QString, ParamInfo> getRequiredParams() const override;

    QString getUniqueReportId(const QString &filePath) const override;
    bool recomputeTaxes() const override;
    bool isWrongIfConflict() const override;
    bool fixRefundDate() const override;
    bool isGroupedOrders() const override;

protected:
    QCoro::Task<ReturnOrderInfos> _loadReport(
        const QString &filePath,
        std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing) override;

private:
    static QDateTime parseCommerceHQDateTime(const QString &dateStr, const QString &timeStr);
    static QDate parseCommerceHQDate(const QString &dateStr);
    static double parseAmount(const QString &str);
};

#endif // IMPORTERFILECOMMERCEHQ_H
