#include "PaneFixVat.h"
#include "ui_PaneFixVat.h"

#include "vatfixer/AbstractVatFixer.h"
#include "ExceptionWithTitleText.h"
#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QSettings>
#include <QTextDocument>
#include <algorithm>

PaneFixVat::PaneFixVat(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneFixVat)
{
    ui->setupUi(this);

    // Populate the fixer list from the self-registered fixers.
    for (const AbstractVatFixer *fixer : AbstractVatFixer::ALL_FIXERS()) {
        ui->listFixers->addItem(fixer->getName());
    }
    if (ui->listFixers->count() > 0) {
        ui->listFixers->setCurrentRow(0);
    }

    _connectSlots();
}

PaneFixVat::~PaneFixVat()
{
    delete ui;
}

const AbstractVatFixer *PaneFixVat::_selectedFixer() const
{
    const int row = ui->listFixers->currentRow();
    const QList<const AbstractVatFixer *> &fixers = AbstractVatFixer::ALL_FIXERS();
    if (row < 0 || row >= fixers.size()) {
        return nullptr;
    }
    return fixers.at(row);
}

void PaneFixVat::onBrowseSummary()
{
    const AbstractVatFixer *fixer = _selectedFixer();
    if (!fixer) {
        QMessageBox::warning(this, tr("No fixer selected"),
                             tr("Please select a VAT fixer first."));
        return;
    }

    QStringList extFilters;
    for (const QString &ext : fixer->getVatSummaryValidExtensions()) {
        extFilters.append(QString("*.%1").arg(ext));
    }

    QSettings settings;
    const QString lastDir = settings.value("fixvat/lastSummaryDir",
                                            QDir::homePath()).toString();

    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        tr("Select Summary Files"),
        lastDir,
        tr("Summary Files (%1)").arg(extFilters.join(' ')));

    if (files.isEmpty()) {
        return;
    }
    settings.setValue("fixvat/lastSummaryDir",
                      QFileInfo(files.first()).absolutePath());

    ui->listSummaryFiles->clear();
    for (const QString &f : files) {
        ui->listSummaryFiles->addItem(f);
    }
}

void PaneFixVat::onBrowseUpdate()
{
    const AbstractVatFixer *fixer = _selectedFixer();
    if (!fixer) {
        QMessageBox::warning(this, tr("No fixer selected"),
                             tr("Please select a VAT fixer first."));
        return;
    }

    QStringList extFilters;
    for (const QString &ext : fixer->getVatOrdersValidExtensions()) {
        extFilters.append(QString("*.%1").arg(ext));
    }

    QSettings settings;
    const QString lastDir = settings.value("fixvat/lastUpdateDir",
                                            QDir::homePath()).toString();

    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        tr("Select Files to Update"),
        lastDir,
        tr("Order Files (%1)").arg(extFilters.join(' ')));

    if (files.isEmpty()) {
        return;
    }
    settings.setValue("fixvat/lastUpdateDir",
                      QFileInfo(files.first()).absolutePath());

    ui->listUpdateFiles->clear();
    for (const QString &f : files) {
        ui->listUpdateFiles->addItem(f);
    }
}

void PaneFixVat::onFixVat()
{
    const AbstractVatFixer *fixer = _selectedFixer();
    if (!fixer) {
        QMessageBox::warning(this, tr("No fixer selected"),
                             tr("Please select a VAT fixer first."));
        return;
    }

    if (ui->listSummaryFiles->count() == 0) {
        QMessageBox::warning(this, tr("No summary files"),
                             tr("Please select at least one summary file."));
        return;
    }
    if (ui->listUpdateFiles->count() == 0) {
        QMessageBox::warning(this, tr("No files to update"),
                             tr("Please select at least one file to update."));
        return;
    }

    QStringList summaryFiles;
    for (int i = 0; i < ui->listSummaryFiles->count(); ++i) {
        summaryFiles.append(ui->listSummaryFiles->item(i)->text());
    }

    QStringList updateFiles;
    for (int i = 0; i < ui->listUpdateFiles->count(); ++i) {
        updateFiles.append(ui->listUpdateFiles->item(i)->text());
    }

    fixer->fixVatOrders(summaryFiles, updateFiles, QStringLiteral("-FIXED"));
}

void PaneFixVat::_connectSlots()
{
    connect(ui->buttonBrowseSummary, &QPushButton::clicked,
            this, &PaneFixVat::onBrowseSummary);
    connect(ui->buttonBrowseUpdate, &QPushButton::clicked,
            this, &PaneFixVat::onBrowseUpdate);
    connect(ui->buttonFixVat, &QPushButton::clicked,
            this, &PaneFixVat::onFixVat);
    connect(ui->buttonFixInventoryCost, &QPushButton::clicked,
            this, &PaneFixVat::onFixInventoryCost);
}

// ── Static helper ─────────────────────────────────────────────────────────────

static void generateInventoryPdfReport(
    const QString &pdfPath,
    const QString &sourceXlsxName,
    const AbstractVatFixer::InventoryFixResult &result)
{
    QString html = QStringLiteral("<html><body>");
    html += QStringLiteral("<h2>") + sourceXlsxName.toHtmlEscaped()
            + QStringLiteral(" \u2014 Inventory Fix Report</h2>");
    html += QStringLiteral(
        "<table border='1' cellspacing='0' cellpadding='4' width='100%'>"
        "<tr><th>SKU</th><th>Price</th><th>Currency</th><th>Source Invoice</th></tr>");
    for (const AbstractVatFixer::InventoryFixResult::FixedSku &fixed
         : std::as_const(result.fixedSkus)) {
        html += QString("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td></tr>")
                .arg(fixed.sku.toHtmlEscaped(),
                     QString::number(fixed.price, 'f', 2),
                     fixed.currency.toHtmlEscaped(),
                     fixed.sourceFile.toHtmlEscaped());
    }
    html += QStringLiteral("</table>");
    if (!result.skusNotFound.isEmpty()) {
        html += QStringLiteral("<h3>SKUs not found (no price available):</h3><p>")
                + result.skusNotFound.join(QStringLiteral(", ")).toHtmlEscaped()
                + QStringLiteral("</p>");
    }
    html += QStringLiteral("</body></html>");

    QTextDocument doc;
    doc.setHtml(html);

    QPdfWriter writer(pdfPath);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);
    doc.print(&writer);
}

// ── onFixInventoryCost ────────────────────────────────────────────────────────

void PaneFixVat::onFixInventoryCost()
{
    const AbstractVatFixer *fixer = _selectedFixer();
    if (!fixer) {
        QMessageBox::warning(this, tr("No fixer selected"),
                             tr("Please select a VAT fixer first."));
        return;
    }

    try {
        // Validate that the purchase and inventory folders are configured.
        QSettings settings;
        const QString purchaseFolder =
            settings.value(QStringLiteral("purchases/lastFolder")).toString();
        if (purchaseFolder.isEmpty() || !QDir(purchaseFolder).exists()) {
            ExceptionWithTitleText ex(
                tr("Purchase Folder Not Set"),
                tr("The purchase folder is not configured.\n"
                   "Please set it in the Inventory tab first."));
            ex.raise();
        }
        const QString inventoryFolder =
            settings.value(QStringLiteral("purchases/lastInventoryFolder")).toString();
        if (inventoryFolder.isEmpty() || !QDir(inventoryFolder).exists()) {
            ExceptionWithTitleText ex(
                tr("Inventory Folder Not Set"),
                tr("The inventory folder is not configured.\n"
                   "Please set it in the Inventory tab first."));
            ex.raise();
        }

        // Browse for taxually inventory xlsx files to fix.
        QStringList extensionFilters;
        for (const QString &ext : fixer->getInventoryValidExtensions()) {
            extensionFilters.append(QString("*.%1").arg(ext));
        }
        const QString lastDir =
            settings.value(QStringLiteral("inventory/lastTaxuallyDir"),
                            QDir::homePath()).toString();
        const QStringList files = QFileDialog::getOpenFileNames(
            this,
            tr("Select Taxually Inventory Files"),
            lastDir,
            tr("Inventory Files (%1)").arg(extensionFilters.join(' ')));
        if (files.isEmpty()) {
            return;
        }
        settings.setValue(QStringLiteral("inventory/lastTaxuallyDir"),
                          QFileInfo(files.first()).absolutePath());

        // Collect purchase CSV files, sorted newest-first (so PurchaseCsvLoader
        // gives priority to the most recent price for each SKU).
        QStringList purchaseCsvFiles;
        QDirIterator it(purchaseFolder,
                        QStringList() << QStringLiteral("*.csv")
                                      << QStringLiteral("*.CSV"),
                        QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            purchaseCsvFiles.append(it.next());
        }
        std::sort(purchaseCsvFiles.begin(), purchaseCsvFiles.end(),
                  std::greater<QString>());

        const QDir workingDir = WorkingDirectoryManager::instance()->workingDir();

        QStringList allSkusNotFound;
        setCursor(Qt::WaitCursor);

        for (const QString &filePath : std::as_const(files)) {
            const QFileInfo fi(filePath);
            const QString fixedPath = fi.absolutePath() + "/" +
                                      fi.baseName() + "-FIXED." + fi.suffix();
            const QString pdfPath   = fi.absolutePath() + "/" +
                                      fi.baseName() + "-FIXED.pdf";

            const AbstractVatFixer::InventoryFixResult result =
                fixer->fixInventoryValue(filePath, fixedPath,
                                         purchaseCsvFiles, workingDir);

            generateInventoryPdfReport(pdfPath, fi.fileName(), result);

            allSkusNotFound.append(result.skusNotFound);
        }

        setCursor(Qt::ArrowCursor);

        allSkusNotFound.removeDuplicates();
        if (!allSkusNotFound.isEmpty()) {
            QMessageBox::warning(this,
                tr("SKUs Not Found"),
                tr("The following SKUs had no purchase price and were left empty:\n%1")
                    .arg(allSkusNotFound.join("\n")));
        } else {
            QMessageBox::information(this,
                tr("Done"),
                tr("All inventory files processed successfully."));
        }
    } catch (const ExceptionWithTitleText &e) {
        setCursor(Qt::ArrowCursor);
        QMessageBox::warning(this, e.errorTitle(), e.errorText());
    }
}
