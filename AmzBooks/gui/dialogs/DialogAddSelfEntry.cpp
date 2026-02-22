#include "DialogAddSelfEntry.h"
#include "ui_DialogAddSelfEntry.h"

#include <QMessageBox>

DialogAddSelfEntry::DialogAddSelfEntry(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogAddSelfEntry)
{
    ui->setupUi(this);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &DialogAddSelfEntry::_onAccepted);
}

DialogAddSelfEntry::~DialogAddSelfEntry()
{
    delete ui;
}

void DialogAddSelfEntry::_onAccepted()
{
    if (ui->editName->text().length() <= 3 || ui->editAccount->text().length() <= 3) {
        QMessageBox::warning(this, tr("Invalid Input"),
                             tr("Both Name and Account must be more than 3 characters."));
        return;
    }
    accept();
}

QString DialogAddSelfEntry::getName() const
{
    return ui->editName->text();
}

QString DialogAddSelfEntry::getAccount() const
{
    return ui->editAccount->text();
}
