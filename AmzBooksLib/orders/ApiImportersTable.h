#ifndef APIIMPORTERSTABLE_H
#define APIIMPORTERSTABLE_H

#include "ImporterTable.h"
#include "AbstractImporterApi.h"

class ApiImportersTable : public ImporterTable
{
    Q_OBJECT

public:
    explicit ApiImportersTable(QObject *parent = nullptr);

    AbstractImporterApi *getImporter(const QModelIndex &index) const;
};

#endif // APIIMPORTERSTABLE_H
