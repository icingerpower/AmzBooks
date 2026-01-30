#include "../../common/workingdirectory/WorkingDirectoryManager.h"
#include "../../common/workingdirectory/DialogOpenConfig.h"
#include "../../common/types/types.h"

#include "books/CompanyInfosTable.h"

#include "gui/MainWindow.h"
#include "gui/DialogCompanyInfos.h"

#include <QApplication>
#include <QSet>

int main(int argc, char *argv[])
{
    qRegisterMetaType<QSet<QString>>();
    qRegisterMetaType<QHash<QString, QSet<QString>>>();
    qRegisterMetaType<QMap<QString, bool>>();

    QCoreApplication::setOrganizationName("Icinger Power");
    QCoreApplication::setOrganizationDomain("ecomelitepro.com");
    QCoreApplication::setApplicationName("AmzBook");

    QApplication a(argc, argv);
    WorkingDirectoryManager::instance()->installDarkOrangePalette();
    DialogOpenConfig dialog;
    dialog.exec();
    if (dialog.wasRejected()) {
        return 0;
    }
    CompanyInfosTable compânyInfos{WorkingDirectoryManager::instance()
                                       ->workingDir()};
    if (!compânyInfos.hadData())
    {
        DialogCompanyInfos dialogCompanyInfos;
        dialogCompanyInfos.exec();
        if (dialogCompanyInfos.result() == QDialog::Rejected)
        {
            return 0;
        }
    }

    MainWindow w;
    w.show();
    return a.exec();
}
