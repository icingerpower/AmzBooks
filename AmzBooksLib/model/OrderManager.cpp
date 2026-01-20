#include <QDir>

#include "OrderManager.h"


OrderManager::OrderManager(const QString &workingDirectory)
{
    m_filePathDb = QDir{workingDirectory}.absoluteFilePath("Orders.db");
}

OrderManager::~OrderManager()
{

}
