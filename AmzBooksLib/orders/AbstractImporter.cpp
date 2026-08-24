#include "ExceptionWithTitleText.h"

#include "AbstractImporter.h"

#include "secrets/CredentialManager.h"

namespace {
// All AmzBooks importers share one keychain namespace.
const QString kSecretApplication = QStringLiteral("AmzBooks");
}

const QString AbstractImporter::CHANNEL_AMAZON{"Amazon"};
const QString AbstractImporter::CHANNEL_TEMU{"Temu"};

AbstractImporter::AbstractImporter(const QDir &workingDirectory)
    : m_workingDirectory(workingDirectory)
{
    m_settingPath = m_workingDirectory.absoluteFilePath("importer.ini");
}

void AbstractImporter::setSharedConfigDirectory(const QDir &dir)
{
    m_sharedConfigDirectoryPath = dir.absolutePath();
}

void AbstractImporter::setWorkingDirectory(const QDir &dir)
{
    m_workingDirectory = dir;
    m_settingPath = m_workingDirectory.absoluteFilePath("importer.ini");
}

QStringList AbstractImporter::getImportedIds() const
{
    return _settings()->value("Reports/ImportedIds").toStringList();
}

const QMap<QString, AbstractImporter::ParamInfo> &AbstractImporter::getLoadedParamValues() const
{
    return m_params;
}

void AbstractImporter::load()
{
    m_params = getRequiredParams();
    auto settings = _settings();

    const QString label = getLabel();
    CredentialManager cred(kSecretApplication);
    const bool storeOk = cred.storeAvailable();

    for (auto it = m_params.begin(); it != m_params.end(); ++it) {
        const QString settingsKey = label + "/" + it.key();
        if (it.value().type == ParamType::RecordList) {
            QVariantList rows;
            const int n = settings->beginReadArray(settingsKey);
            for (int i = 0; i < n; ++i) {
                settings->setArrayIndex(i);
                QVariantMap row;
                for (const auto &field : std::as_const(it.value().fields)) {
                    QString v;
                    if (field.secret && storeOk) {
                        bool found = false;
                        const QString s = cred.get(settingsKey + "/" + QString::number(i) + "/" + field.key, &found);
                        if (found) {
                            v = s;
                        }
                    }
                    if (v.isEmpty() && !field.secret) {
                        v = settings->value(field.key).toString();
                    } else if (v.isEmpty()) {
                        // secret fallback to legacy plaintext (pre-keychain)
                        v = settings->value(field.key).toString();
                    }
                    row.insert(field.key, v);
                }
                rows.append(row);
            }
            settings->endArray();
            it.value().value = rows;
        } else if (it.value().secret) {
            // Secrets live in the keychain. Fall back to a legacy plaintext ini
            // entry only if the keychain has nothing yet (pre-migration state).
            QString v;
            if (storeOk) {
                bool found = false;
                const QString stored = cred.get(settingsKey, &found);
                if (found) {
                    v = stored;
                }
            }
            if (v.isEmpty() && settings->contains(settingsKey)) {
                v = settings->value(settingsKey).toString();
            }
            it.value().value = v.isEmpty() ? it.value().defaultValue : QVariant(v);
        } else if (settings->contains(settingsKey)) {
            it.value().value = settings->value(settingsKey);
        } else {
            it.value().value = it.value().defaultValue;
        }
    }
}

void AbstractImporter::setParam(const QString& key, const QVariant& value)
{
    if (!m_params.contains(key)) {
        ExceptionWithTitleText exception("Unknown Parameter", 
                                  QString("The parameter '%1' is not recognized by this importer.").arg(key));
        exception.raise();
    }
    
    ParamInfo &info = m_params[key];
    
    // Validation
    if (info.validator) {
        auto result = info.validator(value);
        if (!result.first) {
            ExceptionWithTitleText exception("Invalid Value", 
                                      QString("Value for '%1' is invalid: %2").arg(info.label, result.second));
            exception.raise();
        }
    }
    
    info.value = value;

    // Save
    const QString settingsKey = getLabel() + "/" + key;
    if (info.secret) {
        CredentialManager cred(kSecretApplication);
        if (cred.storeAvailable()) {
            QString err;
            if (!cred.set(settingsKey, value.toString(), &err)) {
                ExceptionWithTitleText exception("Keychain Error",
                    QString("Could not store secret '%1': %2").arg(info.label, err));
                exception.raise();
            }
            // Never leave a plaintext copy behind in importer.ini.
            auto settings = _settings();
            if (settings->contains(settingsKey)) {
                settings->remove(settingsKey);
            }
            return;
        }
        // No keyring available: fall back to ini so the app still functions.
    }
    _settings()->setValue(settingsKey, value);
}

QVariant AbstractImporter::getParam(const QString& key) const
{
     if (!m_params.contains(key)) {
        return QVariant();
    }
    return m_params[key].value;
}

QList<QVariantMap> AbstractImporter::getParamRecords(const QString &key) const
{
    if (!m_params.contains(key) || m_params[key].type != ParamType::RecordList) {
        return {};
    }
    QList<QVariantMap> records;
    const QVariantList rows = m_params[key].value.toList();
    records.reserve(rows.size());
    for (const auto &row : std::as_const(rows)) {
        records.append(row.toMap());
    }
    return records;
}

void AbstractImporter::setParamRecords(const QString &key, const QList<QVariantMap> &records)
{
    if (!m_params.contains(key)) {
        ExceptionWithTitleText exception("Unknown Parameter",
                                  QString("The parameter '%1' is not recognized by this importer.").arg(key));
        exception.raise();
    }
    if (m_params[key].type != ParamType::RecordList) {
        ExceptionWithTitleText exception("Invalid Value",
                                  QString("The parameter '%1' is not a record list.").arg(key));
        exception.raise();
    }

    const QString settingsKey = getLabel() + "/" + key;
    auto settings = _settings();
    CredentialManager cred(kSecretApplication);
    const bool storeOk = cred.storeAvailable();
    settings->beginWriteArray(settingsKey, records.size());
    for (int i = 0; i < records.size(); ++i) {
        settings->setArrayIndex(i);
        for (const auto &field : std::as_const(m_params[key].fields)) {
            const QString v = records[i].value(field.key).toString();
            if (field.secret && storeOk) {
                QString err;
                if (!cred.set(settingsKey + "/" + QString::number(i) + "/" + field.key, v, &err)) {
                    ExceptionWithTitleText ex("Keychain Error", QString("Could not store secret '%1': %2").arg(field.label, err));
                    ex.raise();
                }
                if (settings->contains(field.key)) {
                    settings->remove(field.key);
                }
            } else {
                settings->setValue(field.key, v);
            }
        }
    }
    settings->endArray();
    QVariantList stored;
    for (const auto &r : std::as_const(records)) {
        stored.append(r);
    }
    m_params[key].value = stored;
}

QSharedPointer<QSettings> AbstractImporter::_settings() const
{
    return QSharedPointer<QSettings>::create(m_settingPath, QSettings::IniFormat);
}
