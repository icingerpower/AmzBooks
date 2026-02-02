#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include "books/JournalTable.h"
#include "PaneSettingsJournal.h"
#include "ui_PaneSettingsJournal.h"

PaneSettingsJournal::PaneSettingsJournal(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PaneSettingsJournal)
{
    ui->setupUi(this);

    QDir workingDir{WorkingDirectoryManager::instance()->workingDir()};

    auto *journalTable = new JournalTable{workingDir, ui->tableJournals};
    ui->tableJournals->setModel(journalTable);
}

PaneSettingsJournal::~PaneSettingsJournal()
{
    delete ui;
}
