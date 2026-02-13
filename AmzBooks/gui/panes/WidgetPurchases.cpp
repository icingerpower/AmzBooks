#include "WidgetPurchases.h"
#include "ui_WidgetPurchases.h"
#include "gui/dialogs/DialogEditCsvPurchases.h"
#include "profit/PurchaseFileSettingsTree.h"
#include <QFileDialog>
#include <QSettings>
#include <QDebug>
#include <QMessageBox>
#include <QTextStream>
#include <QDir>
#include <QDirIterator>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QDialogButtonBox>

struct ErrorEntry {
    QString file;
    QString error;
};

WidgetPurchases::WidgetPurchases(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::WidgetPurchases),
    m_fileModel(new QFileSystemModel(this))
{
    ui->setupUi(this);

    m_fileModel->setNameFilters(QStringList() << "*.csv" << "*.CSV");
    m_fileModel->setNameFilterDisables(false);
    
    ui->treeViewCsvFiles->setModel(m_fileModel);
    ui->treeViewCsvFiles->setAnimated(false);
    ui->treeViewCsvFiles->setIndentation(20);
    ui->treeViewCsvFiles->setSortingEnabled(true);
    // Hide columns other than Name (Size, Type, Date Modified) if desired, 
    // but usually they are useful. Let's keep them.
    
    // Load saved folder
    QSettings settings;
    m_currentDir = settings.value("purchases/lastFolder").toString();
    
    // Ensure usage of a valid directory
    if (!m_currentDir.isEmpty() && QDir(m_currentDir).exists()) {
        onFolderChanged(m_currentDir);
    } else {
        m_currentDir.clear();
        ui->treeViewCsvFiles->setModel(nullptr); 
    }

    _connectSlots();
}

WidgetPurchases::~WidgetPurchases()
{
    delete ui;
}

void WidgetPurchases::_connectSlots()
{
    connect(ui->buttonSelectFolder, &QPushButton::clicked, this, &WidgetPurchases::onSelectFolder);
    connect(ui->buttonEditCsv, &QPushButton::clicked, this, &WidgetPurchases::onEditColumns);
    connect(ui->pushButton, &QPushButton::clicked, this, &WidgetPurchases::onCheckFiles);
}

void WidgetPurchases::onSelectFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Purchase Folder"),
                                                    m_currentDir,
                                                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) {
        onFolderChanged(dir);
        
        // Save settings
        QSettings settings;
        settings.setValue("purchases/lastFolder", m_currentDir);
    }
}

void WidgetPurchases::onFolderChanged(const QString &path)
{
    m_currentDir = path;
    ui->lineEditPurchaseFolder->setText(m_currentDir);
    
    m_fileModel->setRootPath(m_currentDir);
    ui->treeViewCsvFiles->setRootIndex(m_fileModel->index(m_currentDir));
}

void WidgetPurchases::onEditColumns()
{
    DialogEditCsvPurchases dialog(m_currentDir, this);
    dialog.exec();
}

#include <QDirIterator>

void WidgetPurchases::onCheckFiles()
{
    // Find all CSV files recursively
    QStringList files = getCsvFilePaths();
    
    if (files.isEmpty()) {
        QMessageBox::information(this, tr("Check Files"), tr("No CSV files found in the current directory or subdirectories."));
        return;
    }
    
    
    PurchaseFileSettingsTree settingsTree((QDir(m_currentDir)));
    QList<ErrorEntry> errors;
    
    foreach (const QString &filePath, files) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            errors.append({QDir(m_currentDir).relativeFilePath(filePath), "Cannot open file."});
            continue;
        }
        
        QTextStream in(&file);
        QString headerLine = in.readLine();
        file.close();
        
        if (headerLine.isEmpty()) {
            errors.append({QDir(m_currentDir).relativeFilePath(filePath), "Empty header."});
            continue;
        }
        
        // Detect separator
        QString separator = ";";
        if (headerLine.contains("\t")) separator = "\t";
        else if (headerLine.count(",") > headerLine.count(";")) separator = ",";
        
        QStringList headers = headerLine.split(separator);
        
        // Trim headers
        for (QString &h : headers) h = h.trimmed();
        
        QStringList missingCols;
        for (const QString &fixedId : PurchaseFileSettingsTree::FIXED_ROW_IDS) {
            if (settingsTree.getColPos(headers, fixedId) == -1) {
                missingCols << fixedId; 
            }
        }
        
        if (!missingCols.isEmpty()) {
            errors.append({QDir(m_currentDir).relativeFilePath(filePath), "Missing columns: " + missingCols.join(", ") + " (" + headerLine + ")"});
        }
    }
    
    if (errors.isEmpty()) {
        QMessageBox::information(this, tr("Check Files"), tr("All files checked successfully. No missing columns found."));
    } else {
        // Show report in a custom dialog with QTableWidget
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Check Results"));
        dialog.resize(800, 400); // 1. Resizable (default QDialog is resizable) and set reasonable size
        
        QVBoxLayout *layout = new QVBoxLayout(&dialog);
        
        QTableWidget *table = new QTableWidget(&dialog);
        table->setColumnCount(2);
        table->setHorizontalHeaderLabels(QStringList() << tr("File") << tr("Issue"));
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents); // File path might be long
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch); // Issue takes remaining space
        table->setRowCount(errors.size());
        
        for (int i = 0; i < errors.size(); ++i) {
            QTableWidgetItem *itemFile = new QTableWidgetItem(errors[i].file);
            QTableWidgetItem *itemError = new QTableWidgetItem(errors[i].error);
            itemFile->setFlags(itemFile->flags() ^ Qt::ItemIsEditable); // Read-only
            itemError->setFlags(itemError->flags() ^ Qt::ItemIsEditable);
            
            table->setItem(i, 0, itemFile);
            table->setItem(i, 1, itemError);
        }
        
        layout->addWidget(table);
        
        QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttonBox);
        
        dialog.exec();
    }
}

QStringList WidgetPurchases::getCsvFilePaths() const
{
    if (m_currentDir.isEmpty()) return QStringList();
    
    QStringList absolutePaths;
    QDirIterator it(m_currentDir, QStringList() << "*.csv" << "*.CSV", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        absolutePaths << it.next();
    }
    
    // Sort reverse order
    std::sort(absolutePaths.begin(), absolutePaths.end(), std::greater<QString>());
    
    return absolutePaths;
}
