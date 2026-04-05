#include "AbstractVatFixer.h"

QList<const AbstractVatFixer *> &AbstractVatFixer::getFixers()
{
    static QList<const AbstractVatFixer *> fixers;
    return fixers;
}

const QList<const AbstractVatFixer *> &AbstractVatFixer::ALL_FIXERS()
{
    return getFixers();
}

AbstractVatFixer::Recorder::Recorder(AbstractVatFixer *fixer)
{
    getFixers().append(fixer);
}
