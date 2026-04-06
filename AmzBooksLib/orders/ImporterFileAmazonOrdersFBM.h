#ifndef IMPORTERFILEAMAZONORDERSFBM_H
#define IMPORTERFILEAMAZONORDERSFBM_H

#include "AbstractImporterFile.h"

// Importer for Amazon FBM (Fulfilled By Merchant) order reports.
// Format: tab-separated, one row per order item.
// Origin country is derived from the sales-channel column (e.g. "Amazon.com" → "US").
// For US, CA and MX marketplaces, Amazon acts as marketplace facilitator: item-tax and
// shipping-tax must be zeroed. The activity amount includes both item revenue and
// shipping revenue to capture total seller income in one activity.
class ImporterFileAmazonOrdersFBM : public AbstractImporterFile
{
public:
    using AbstractImporterFile::AbstractImporterFile;
    ImporterFileAmazonOrdersFBM() : AbstractImporterFile() {}

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
    // Maps a sales-channel string (e.g. "Amazon.com") to an ISO 3166-1 alpha-2 country code.
    // Returns an empty string if the channel is not recognised.
    static QString salesChannelToCountryCode(const QString &salesChannel);
};

#endif // IMPORTERFILEAMAZONORDERSFBM_H
