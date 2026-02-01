#include "AbstractBankStatement.h"


// QMap<QString, const AbstractBankStatement *> AbstractBankStatement::_BANKS;

QMap<QString, const AbstractBankStatement *> &AbstractBankStatement::getBanks()
{
    static QMap<QString, const AbstractBankStatement *> banks;
    return banks;
}

const QMap<QString, const AbstractBankStatement *> &AbstractBankStatement::ALL_BANKS()
{
    return getBanks();
}

AbstractBankStatement::AbstractBankStatement()
{

}

QStringList AbstractBankStatement::fileFilters() const
{
    return QStringList{"*.csv", "*.CSV"};
}

AbstractBankStatement::Recorder::Recorder(const AbstractBankStatement *dataGetter)
{
    getBanks()[dataGetter->getName()] = dataGetter;
}
