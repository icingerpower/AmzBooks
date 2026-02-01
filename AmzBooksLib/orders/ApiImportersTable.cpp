#include "ApiImportersTable.h"

ApiImportersTable::ApiImportersTable(QObject *parent)
    : ImporterTable(parent)
{
    const auto &importers = AbstractImporterApi::ALL_IMPORTERS();
    for (const auto &importer : importers) {
        m_importers.append(importer);
    }
    sortImporters();
}

AbstractImporterApi *ApiImportersTable::getImporter(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() >= m_importers.count())
        return nullptr;
    
    // cast away constness because the user asked for AbstractImporterApi* return type
    return const_cast<AbstractImporterApi*>(static_cast<const AbstractImporterApi*>(m_importers.at(index.row())));
}
