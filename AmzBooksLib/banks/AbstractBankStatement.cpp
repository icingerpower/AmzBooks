#include "AbstractBankStatement.h"


// QMap<QString, const AbstractBankStatement *> AbstractBankStatement::_BANKS;

QMap<QString, AbstractBankStatement *> &AbstractBankStatement::getBanks()
{
    static QMap<QString, AbstractBankStatement *> banks;
    return banks;
}

const QMap<QString, AbstractBankStatement *> &AbstractBankStatement::ALL_BANKS()
{
    return getBanks();
}

AbstractBankStatement::AbstractBankStatement(QObject *parent)
    : QObject(parent)
{

}

QStringList AbstractBankStatement::fileFilters() const
{
    return QStringList{"*.csv", "*.CSV"};
}

QString AbstractBankStatement::hasWarnings(const QString &filePath) const
{
    Q_UNUSED(filePath)
    return QString{};
}

QString AbstractBankStatement::defaultJournal() const
{
    return "BQ";
}

AbstractBankStatement::Recorder::Recorder(const QString& id, AbstractBankStatement* statement)
{
    getBanks()[id] = statement;
}
