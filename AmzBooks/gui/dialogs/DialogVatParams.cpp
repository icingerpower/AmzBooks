#include "DialogVatParams.h"
#include "ui_DialogVatParams.h"

#include "../panes/PaneSettingsVatRates.h"
#include "../panes/PaneSaleAccounts.h"
#include "../panes/PaneFbaCenters.h"

DialogVatParams::DialogVatParams(const QString &errorTitle,
                                 const QString &errorText,
                                 QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogVatParams)
{
    ui->setupUi(this);
    
    setWindowTitle(errorTitle);
    ui->labelErrorDescription->setText(errorText);
    
    // Add panes to stacked widget
    auto *paneVatRates = new PaneSettingsVatRates(this);
    auto *paneSaleAccounts = new PaneSaleAccounts(this);
    auto *paneFbaCenters = new PaneFbaCenters(this);
    
    ui->stackedWidget->addWidget(paneVatRates);
    ui->stackedWidget->addWidget(paneSaleAccounts);
    ui->stackedWidget->addWidget(paneFbaCenters);
    
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    
    // Select first item by default
    if (ui->tableNavigation->count() > 0) {
        ui->tableNavigation->setCurrentRow(0);
    }
}

DialogVatParams::~DialogVatParams()
{
    delete ui;
}

