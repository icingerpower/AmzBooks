#include "DialogEditServiceClients.h"
#include "ui_DialogEditServiceClients.h"
#include "books/ServiceClientManager.h"
#include <QMessageBox>
#include <QInputDialog>

// DialogEditServiceClients Implementation

DialogEditServiceClients::DialogEditServiceClients(ServiceClientManager *clientManager, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogEditServiceClients),
    m_clientManager(clientManager)
{
    ui->setupUi(this);
    
    // Set Model
    ui->tableView->setModel(m_clientManager);
    ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableView->setAlternatingRowColors(true);
    ui->tableView->horizontalHeader()->setStretchLastSection(true);
    
    _setupConnections();
}

DialogEditServiceClients::~DialogEditServiceClients()
{
    delete ui;
}

void DialogEditServiceClients::_setupConnections()
{
    connect(ui->buttonAdd, &QPushButton::clicked, this, &DialogEditServiceClients::addClient);
    connect(ui->buttonRemove, &QPushButton::clicked, this, &DialogEditServiceClients::removeClient);
    connect(ui->buttonClose, &QPushButton::clicked, this, &DialogEditServiceClients::accept);
}

void DialogEditServiceClients::addClient()
{
    // Simple Input Dialogs for now, or could create another form dialog.
    // For simplicity given the requirements: "Button add/remove in top left"
    // Let's ask for Name at least, others can be edited in table?
    // Wait, ServiceClientManager::setData might not be fully implemented or we prefer a proper way.
    // Looking at ServiceClientManager::addClient signature:
    // void addClient(const QString &clientName, const QString &serviceLabel, 
    //                const QString &country, const QString &vatNumber, 
    //                const QString &currency, double defaultAmount);
    
    // We can add an empty/default row and let user edit it in the TableView if flags allow.
    // ServiceClientManager::flags needs to allow ItemIsEditable.
    
    // Let's create a default new client
    QString name = QInputDialog::getText(this, tr("New Client"), tr("Client Name:"));
    if (name.isEmpty()) return;
    
    m_clientManager->addClient(name, "Service", "FR", "", "EUR", 0.0);
}

void DialogEditServiceClients::removeClient()
{
    QModelIndexList selection = ui->tableView->selectionModel()->selectedRows();
    if (selection.isEmpty()) {
        QMessageBox::warning(this, tr("No Selection"), tr("Please select a client to remove."));
        return;
    }
    
    // Remove the first selected row (SingleSelection mode)
    int row = selection.first().row();
    m_clientManager->removeClient(row);
}
