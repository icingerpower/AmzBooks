#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include "books/JournalTable.h"
#include "PaneJournals.h"
#include "ui_PaneJournals.h"

PaneJournals::PaneJournals(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PaneJournals)
{
    ui->setupUi(this);

    QDir workingDir{WorkingDirectoryManager::instance()->workingDir()};

    auto *journalTable = new JournalTable{workingDir, ui->tableJournals};
    ui->tableJournals->setModel(journalTable);
}

PaneJournals::~PaneJournals()
{
    delete ui;
}
