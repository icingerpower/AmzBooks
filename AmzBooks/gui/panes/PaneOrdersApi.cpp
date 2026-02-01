#include "PaneOrdersApi.h"
#include "ui_PaneOrdersApi.h"

PaneOrdersApi::PaneOrdersApi(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PaneOrdersApi)
{
    ui->setupUi(this);
}

PaneOrdersApi::~PaneOrdersApi()
{
    delete ui;
}
