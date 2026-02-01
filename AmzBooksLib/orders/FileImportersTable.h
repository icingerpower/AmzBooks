#ifndef FILEIMPORTERSTABLE_H
#define FILEIMPORTERSTABLE_H

#include "ImporterTable.h"
#include "AbstractImporterFile.h"

class FileImportersTable : public ImporterTable
{
    Q_OBJECT

public:
    explicit FileImportersTable(QObject *parent = nullptr);

    AbstractImporterFile *getImporter(const QModelIndex &index) const;
};


#endif // FILEIMPORTERSTABLE_H
