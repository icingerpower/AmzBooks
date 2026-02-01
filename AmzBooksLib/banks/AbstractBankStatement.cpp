#include "AbstractBankStatement.h"


QMap<QString, const AbstractBankStatement *> AbstractBankStatement::_BANKS;

const QMap<QString, const AbstractBankStatement *> &AbstractBankStatement::ALL_BANKS()
{
    return _BANKS;
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
    _BANKS[dataGetter->getName()] = dataGetter;
}
