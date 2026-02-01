#include "FileImportersTable.h"
#include "AbstractImporterFile.h"

FileImportersTable::FileImportersTable(QObject *parent)
    : ImporterTable(parent)
{
    const auto &importers = AbstractImporterFile::ALL_IMPORTERS();
    for (const auto &importer : importers) {
        m_importers.append(importer);
    }
    sortImporters();
}



AbstractImporterFile *FileImportersTable::getImporter(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() >= m_importers.count())
        return nullptr;
    
    // cast away constness because the user asked for AbstractImporterFile* return type
    return const_cast<AbstractImporterFile*>(static_cast<const AbstractImporterFile*>(m_importers.at(index.row())));
}
