#include "DialogVatParams.h"
#include "ui_DialogVatParams.h"

#include "gui/panes/PaneSettingsVatRates.h"
#include "gui/panes/PaneSettingsSaleAccounts.h"
#include "gui/panes/PaneSettingsFbaCenters.h"
#include "gui/panes/PaneSettingsAmzBalanceAccounts.h"

DialogVatParams::DialogVatParams(const QString &errorTitle,
                                 const QString &errorText,
                                 QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogVatParams)
{
    ui->setupUi(this);
    
    setWindowTitle(errorTitle);
    ui->labelErrorDescription->setText(errorText);
    ui->labelErrorDescription->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    
    // Add panes to stacked widget
    auto *paneVatRates = new PaneSettingsVatRates(this);
    auto *paneSaleAccounts = new PaneSettingsSaleAccounts(this);
    auto *paneFbaCenters = new PaneSettingsFbaCenters(this);
    auto *paneAmzBalances = new PaneSettingsAmzBalanceAccounts(this);

    ui->stackedWidget->addWidget(paneVatRates);
    ui->stackedWidget->addWidget(paneSaleAccounts);
    ui->stackedWidget->addWidget(paneFbaCenters);
    ui->stackedWidget->addWidget(paneAmzBalances);
    
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

