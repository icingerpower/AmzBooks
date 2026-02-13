#include "PaneProfit.h"
#include "ui_PaneProfit.h"
#include <QFileDialog>
#include <QSettings>
#include <QDir>

const QString PaneProfit::SETTINGS_KEY_ECONOMIC_FOLDER = "profit/economicFolder";

PaneProfit::PaneProfit(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PaneProfit)
{
    ui->setupUi(this);
    
    QSettings settings;
    QString economicFolder = settings.value(SETTINGS_KEY_ECONOMIC_FOLDER).toString();
    if (!economicFolder.isEmpty() && QDir(economicFolder).exists()) {
        ui->lineEditEconomicsFolder->setText(economicFolder);
    }
    
    _connectSlots();
}

PaneProfit::~PaneProfit()
{
    delete ui;
}

void PaneProfit::browseEconomicFolder()
{
    QString currentDir = ui->lineEditEconomicsFolder->text();
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Economic Folder"),
                                                    currentDir,
                                                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    
    if (!dir.isEmpty()) {
        ui->lineEditEconomicsFolder->setText(dir);
        
        QSettings settings;
        settings.setValue(SETTINGS_KEY_ECONOMIC_FOLDER, dir);
    }
}

void PaneProfit::_connectSlots()
{
    connect(ui->buttonBrowseEconomicsFolder,
            &QPushButton::clicked,
            this,
            &PaneProfit::browseEconomicFolder);
}
