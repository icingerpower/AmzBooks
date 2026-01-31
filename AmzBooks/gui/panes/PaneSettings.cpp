#include "PaneSettings.h"
#include "ui_PaneSettings.h"

PaneSettings::PaneSettings(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneSettings)
{
    ui->setupUi(this);
}

PaneSettings::~PaneSettings()
{
    delete ui;
}
