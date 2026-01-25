#include "ImporterApiTemu.h"
#include <QJsonParseError>

ActivitySource ImporterApiTemu::getActivitySource() const
{
    // Temu source. Subchannel will be dynamic or generic.
    // Since we aggregate multiple shops, subchannel might be "Aggregated" or we rely on 
    // individual activities having specific source data if Activity supported it.
    // For now, generic.
    return ActivitySource {
        .type = ActivitySourceType::API,
        .channel = "Temu",
        .subchannel = "Eu", // Default, specific subclasses might override if needed but here we share base
        .reportOrMethode = "OpenAPI"
    };
}

QString ImporterApiTemu::getLabel() const
{
    return "Temu API";
}

QMap<QString, AbstractImporter::ParamInfo> ImporterApiTemu::getRequiredParams() const
{
    QMap<QString, ParamInfo> params;
    
    params["shops"] = ParamInfo {
        .key = "shops",
        .label = "Shops Configuration (JSON)",
        .description = "List of shops in JSON format: [{\"name\":\"Shop1\", \"appId\":\"...\", \"appSecret\":\"...\", \"accessToken\":\"...\"}, ...]",
        .defaultValue = "[]",
        .value = QVariant(),
        .validator = [](const QVariant& v) -> std::pair<bool, QString> {
            QString str = v.toString();
            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(str.toUtf8(), &error);
            if (error.error != QJsonParseError::NoError) {
                return {false, "Invalid JSON: " + error.errorString()};
            }
            if (!doc.isArray()) {
                return {false, "JSON must be an array of shop objects"};
            }
            // Optional: validate content of each object
            return {true, ""};
        }
    };
    
    return params;
}

QList<ImporterApiTemu::ShopConfig> ImporterApiTemu::getShops() const
{
    QList<ShopConfig> shops;
    QString jsonStr = getParam("shops").toString();
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    
    if (doc.isArray()) {
        QJsonArray array = doc.array();
        for (const auto& val : array) {
            QJsonObject obj = val.toObject();
            ShopConfig shop;
            shop.name = obj["name"].toString();
            shop.appId = obj["appId"].toString();
            shop.appSecret = obj["appSecret"].toString();
            shop.accessToken = obj["accessToken"].toString();
            
            if (!shop.name.isEmpty() && !shop.appId.isEmpty()) {
                shops.append(shop);
            }
        }
    }
    return shops;
}
