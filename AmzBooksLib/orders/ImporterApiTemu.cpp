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
        .channel = CHANNEL_TEMU,
        .subchannel = "EU", // Default, specific subclasses might override if needed but here we share base
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
    
    auto arrayValidator = [](const QVariant& v) -> std::pair<bool, QString> {
        QString str = v.toString();
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(str.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError) {
            return {false, "Invalid JSON: " + error.errorString()};
        }
        if (!doc.isArray()) {
            return {false, "Value must be a JSON array of strings"};
        }
        // Check if all elements are strings
        QJsonArray arr = doc.array();
        for (const auto& kv : arr) {
            if (!kv.isString()) {
                return {false, "All elements in the array must be strings"};
            }
        }
        return {true, ""};
    };

    params["shopNames"] = ParamInfo {
        .key = "shopNames",
        .label = "Shop Names (JSON Array)",
        .description = "List of shop names: [\"Shop1\", \"Shop2\"]",
        .defaultValue = "[]",
        .value = QVariant(),
        .validator = arrayValidator
    };
    
    params["appIds"] = ParamInfo {
        .key = "appIds",
        .label = "App IDs (JSON Array)",
        .description = "List of App IDs: [\"bg_123\", \"bg_456\"]",
        .defaultValue = "[]",
        .value = QVariant(),
        .validator = arrayValidator
    };

    params["appSecrets"] = ParamInfo {
        .key = "appSecrets",
        .label = "App Secrets (JSON Array)",
        .description = "List of App Secrets: [\"sk_abc\", \"sk_def\"]",
        .defaultValue = "[]",
        .value = QVariant(),
        .validator = arrayValidator
    };

    params["accessTokens"] = ParamInfo {
        .key = "accessTokens",
        .label = "Access Tokens (JSON Array)",
        .description = "List of Access Tokens: [\"ey_123\", \"ey_456\"]",
        .defaultValue = "[]",
        .value = QVariant(),
        .validator = arrayValidator
    };
    
    return params;
}

QList<ImporterApiTemu::ShopConfig> ImporterApiTemu::getShops() const
{
    QList<ShopConfig> shops;
    
    auto parseArray = [this](const QString& key) -> QStringList {
        QString jsonStr = getParam(key).toString();
        QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
        QStringList list;
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (const auto& val : arr) {
                list.append(val.toString());
            }
        }
        return list;
    };

    QStringList names = parseArray("shopNames");
    QStringList appIds = parseArray("appIds");
    QStringList appSecrets = parseArray("appSecrets");
    QStringList accessTokens = parseArray("accessTokens");

    // We zip based on the minimum length to be safe
    qsizetype count = std::min({names.size(), appIds.size(), appSecrets.size(), accessTokens.size()});

    for (qsizetype i = 0; i < count; ++i) {
        ShopConfig shop;
        shop.name = names[i];
        shop.appId = appIds[i];
        shop.appSecret = appSecrets[i];
        shop.accessToken = accessTokens[i];
        
        if (!shop.name.isEmpty() && !shop.appId.isEmpty()) {
            shops.append(shop);
        }
    }
    return shops;
}
