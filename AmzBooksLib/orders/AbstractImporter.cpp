#include "ExceptionParamValue.h"

#include "AbstractImporter.h"


AbstractImporter::AbstractImporter(const QDir &workingDirectory)
    : m_workingDirectory(workingDirectory)
{
    m_settingPath = m_workingDirectory.absoluteFilePath("importer.ini");
}

const QMap<QString, AbstractImporter::ParamInfo> &AbstractImporter::getLoadedParamValues() const
{
    return m_params;
}

void AbstractImporter::load()
{
    m_params = getRequiredParams();
    auto settings = _settings();
    
    QString label = getLabel();
    for (auto it = m_params.begin(); it != m_params.end(); ++it) {
        QString settingsKey = label + "/" + it.key();
        if (settings->contains(settingsKey)) {
            it.value().value = settings->value(settingsKey);
        } else {
            it.value().value = it.value().defaultValue;
        }
    }
}

void AbstractImporter::setParam(const QString& key, const QVariant& value)
{
    if (!m_params.contains(key)) {
        throw ExceptionParamValue("Unknown Parameter", 
                                  QString("The parameter '%1' is not recognized by this importer.").arg(key));
    }
    
    ParamInfo &info = m_params[key];
    
    // Validation
    if (info.validator) {
        auto result = info.validator(value);
        if (!result.first) {
            throw ExceptionParamValue("Invalid Value", 
                                      QString("Value for '%1' is invalid: %2").arg(info.label, result.second));
        }
    }
    
    info.value = value;
    
    // Save
    auto settings = _settings();
    settings->setValue(getLabel() + "/" + key, value);
}

QVariant AbstractImporter::getParam(const QString& key) const
{
     if (!m_params.contains(key)) {
        return QVariant();
    }
    return m_params[key].value;
}

QSharedPointer<QSettings> AbstractImporter::_settings() const
{
    return QSharedPointer<QSettings>::create(m_settingPath, QSettings::IniFormat);
}
