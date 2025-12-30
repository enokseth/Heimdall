/* Copyright (c) 2010-2017 Benjamin Dobell, Glass Echidna
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.*/

// Qt
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QProcess>
#include <QRegExp>
#include <QUrl>
#include <QDesktopServices>
#include <QStandardPaths>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

// Heimdall Frontend
#include "Alerts.h"
#include "mainwindow.h"
#include "Packaging.h"
#include "AdbCommands.h"
#include "TEEAnalyzer.h"

#define UNUSED(x) (void)(x)

using namespace HeimdallFrontend;

void MainWindow::StartHeimdall(const QStringList& arguments)
{
	UpdateInterfaceAvailability();

	heimdallProcess.setReadChannel(QProcess::StandardOutput);

	// Echo the exact command we're about to run to help debugging
	QString cmdPreview = "heimdall " + arguments.join(' ');
	if (!!(heimdallState & HeimdallState::Flashing))
	{
		outputPlainTextEdit->appendPlainText("Executing: " + cmdPreview + "\n");
	}
	else
	{
		utilityOutputPlainTextEdit->appendPlainText("Executing: " + cmdPreview + "\n");
	}
	
	heimdallProcess.start("heimdall", arguments);
	heimdallProcess.waitForStarted(3000);
	
	// OS X was playing up and not finding heimdall, so we're manually checking the PATH.
	if (heimdallFailed)
	{
		QStringList environment = QProcess::systemEnvironment();
		
		QStringList paths;

		// Ensure /usr/local/bin and /usr/bin are in PATH.
		for (int i = 0; i < environment.length(); i++)
		{
			if (environment[i].left(5) == "PATH=")
			{
				paths = environment[i].mid(5).split(':');
				
				if (!paths.contains("/usr/local/bin"))
					paths.prepend("/usr/local/bin");
				
				if (!paths.contains("/usr/bin"))
					paths.prepend("/usr/bin");
				
				break;
			}
		}
		
		int pathIndex = -1;

		while (heimdallFailed && ++pathIndex < paths.length())
		{
			QString heimdallPath = paths[pathIndex];
			
			if (heimdallPath.length() > 0)
			{
				utilityOutputPlainTextEdit->clear();
				heimdallFailed = false;
				
				if (heimdallPath[heimdallPath.length() - 1] != QDir::separator())
					heimdallPath += QDir::separator();
				
				heimdallPath += "heimdall";
				
				heimdallProcess.start(heimdallPath, arguments);
				heimdallProcess.waitForStarted(3000);
			}
		}
		
		if (heimdallFailed)
		{
			flashLabel->setText("Failed to start Heimdall!");
			
			heimdallState = HeimdallState::Stopped;
			UpdateInterfaceAvailability();
		}
	}
}

void MainWindow::UpdateUnusedPartitionIds(void)
{
	unusedPartitionIds.clear();

	// Initially populate unusedPartitionIds with all possible partition IDs. 
	for (unsigned int i = 0; i < currentPitData.GetEntryCount(); i++)
	{
		const PitEntry *pitEntry = currentPitData.GetEntry(i);

		if (pitEntry->IsFlashable() && strcmp(pitEntry->GetPartitionName(), "PIT") != 0 && strcmp(pitEntry->GetPartitionName(), "PT") != 0)
			unusedPartitionIds.append(pitEntry->GetIdentifier());
	}

	// Remove any used partition IDs from unusedPartitionIds
	QList<FileInfo>& fileList = workingPackageData.GetFirmwareInfo().GetFileInfos();

	for (int i = 0; i < fileList.length(); i++)
		unusedPartitionIds.removeOne(fileList[i].GetPartitionId());
}

bool MainWindow::ReadPit(QFile *file)
{
	if(!file->open(QIODevice::ReadOnly))
		return (false);

	unsigned char *buffer = new unsigned char[file->size()];

	file->read(reinterpret_cast<char *>(buffer), file->size());
	file->close();

	bool success = currentPitData.Unpack(buffer);
	delete[] buffer;

	if (!success)
		currentPitData.Clear();

	return (success);
}

void MainWindow::UpdatePackageUserInterface(void)
{
	supportedDevicesListWidget->clear();
	includedFilesListWidget->clear();

	if (loadedPackageData.IsCleared())
	{
		// Package Interface
		firmwareNameLineEdit->clear();
		versionLineEdit->clear();

		developerNamesLineEdit->clear();

		platformLineEdit->clear();
		
		repartitionRadioButton->setChecked(false);
		noRebootRadioButton->setChecked(false);
	}
	else
	{
		firmwareNameLineEdit->setText(loadedPackageData.GetFirmwareInfo().GetName());
		versionLineEdit->setText(loadedPackageData.GetFirmwareInfo().GetVersion());

		QString developerNames;

		if (!loadedPackageData.GetFirmwareInfo().GetDevelopers().isEmpty())
		{
			developerNames = loadedPackageData.GetFirmwareInfo().GetDevelopers()[0];
			for (int i = 1; i < loadedPackageData.GetFirmwareInfo().GetDevelopers().length(); i++)
				developerNames += ", " + loadedPackageData.GetFirmwareInfo().GetDevelopers()[i];
		}

		developerNamesLineEdit->setText(developerNames);

		platformLineEdit->setText(loadedPackageData.GetFirmwareInfo().GetPlatformInfo().GetName() + " ("
			+ loadedPackageData.GetFirmwareInfo().GetPlatformInfo().GetVersion() + ")");

		for (int i = 0; i < loadedPackageData.GetFirmwareInfo().GetDeviceInfos().length(); i++)
		{
			const DeviceInfo& deviceInfo = loadedPackageData.GetFirmwareInfo().GetDeviceInfos()[i];
			supportedDevicesListWidget->addItem(deviceInfo.GetManufacturer() + " " + deviceInfo.GetName() + ": " + deviceInfo.GetProduct());
		}

		for (int i = 0; i < loadedPackageData.GetFirmwareInfo().GetFileInfos().length(); i++)
		{
			const FileInfo& fileInfo = loadedPackageData.GetFirmwareInfo().GetFileInfos()[i];
			includedFilesListWidget->addItem(fileInfo.GetFilename());
		}

		repartitionRadioButton->setChecked(loadedPackageData.GetFirmwareInfo().GetRepartition());
		noRebootRadioButton->setChecked(loadedPackageData.GetFirmwareInfo().GetNoReboot());
	}

	UpdateLoadPackageInterfaceAvailability();
}

bool MainWindow::IsArchive(QString path)
{
	// Not a real check but hopefully it gets the message across, don't directly flash archives!
	return (path.endsWith(".tar", Qt::CaseInsensitive) || path.endsWith(".gz", Qt::CaseInsensitive) || path.endsWith(".zip", Qt::CaseInsensitive)
		|| path.endsWith(".bz2", Qt::CaseInsensitive) || path.endsWith(".7z", Qt::CaseInsensitive) || path.endsWith(".rar", Qt::CaseInsensitive));
}

QString MainWindow::PromptFileSelection(const QString& caption, const QString& filter)
{
	QString path = QFileDialog::getOpenFileName(this, caption, lastDirectory, filter);

	if (path != "")
		lastDirectory = path.left(path.lastIndexOf('/') + 1);

	return (path);
}

QString MainWindow::PromptFileCreation(const QString& caption, const QString& filter)
{
	QString path = QFileDialog::getSaveFileName(this, caption, lastDirectory, filter);

	if (path != "")
		lastDirectory = path.left(path.lastIndexOf('/') + 1);

	return (path);
}

void MainWindow::UpdateLoadPackageInterfaceAvailability(void)
{
	if (loadedPackageData.IsCleared())
	{
		developerHomepageButton->setEnabled(false);
		developerDonateButton->setEnabled(false);
		loadFirmwareButton->setEnabled(false);
	}
	else
	{
		developerHomepageButton->setEnabled(!loadedPackageData.GetFirmwareInfo().GetUrl().isEmpty());
		developerDonateButton->setEnabled(!loadedPackageData.GetFirmwareInfo().GetDonateUrl().isEmpty());
		loadFirmwareButton->setEnabled(!!(heimdallState & HeimdallState::Stopped));
	}
}

void MainWindow::UpdateFlashInterfaceAvailability(void)
{
	if (!!(heimdallState & HeimdallState::Stopped))
	{
		partitionNameComboBox->setEnabled(partitionsListWidget->currentRow() >= 0);

		bool allPartitionsValid = true;
		QList<FileInfo>& fileList = workingPackageData.GetFirmwareInfo().GetFileInfos();

		// Clarify repartition behavior for single vs multi-part flashes
		if (fileList.length() == 1)
			repartitionCheckBox->setToolTip("Repartition is skipped when flashing a single partition.");
		else
			repartitionCheckBox->setToolTip("Repartitioning will wipe all data for your phone and install the selected PIT file.");

		for (int i = 0; i < fileList.length(); i++)
		{
			if (fileList[i].GetFilename().isEmpty())
			{
				allPartitionsValid = false;
				break;
			}
		}

		bool validFlashSettings = allPartitionsValid && fileList.length() > 0;
		
		flashProgressBar->setEnabled(false);
		optionsGroup->setEnabled(true);
		sessionGroup->setEnabled(true);
		startFlashButton->setEnabled(validFlashSettings);
		noRebootCheckBox->setEnabled(validFlashSettings);
		resumeCheckbox->setEnabled(validFlashSettings);
	}
	else
	{
		partitionNameComboBox->setEnabled(false);

		flashProgressBar->setEnabled(true);
		optionsGroup->setEnabled(false);
		sessionGroup->setEnabled(false);
	}
}

void MainWindow::UpdateCreatePackageInterfaceAvailability(void)
{
	if (!!(heimdallState & HeimdallState::Stopped))
	{
		const FirmwareInfo& firmwareInfo = workingPackageData.GetFirmwareInfo();

		bool fieldsPopulated = !(firmwareInfo.GetName().isEmpty() || firmwareInfo.GetVersion().isEmpty() || firmwareInfo.GetPlatformInfo().GetName().isEmpty()
			|| firmwareInfo.GetPlatformInfo().GetVersion().isEmpty() || firmwareInfo.GetDevelopers().isEmpty() || firmwareInfo.GetDeviceInfos().isEmpty());

		buildPackageButton->setEnabled(fieldsPopulated);
		addDeveloperButton->setEnabled(!addDeveloperButton->text().isEmpty());
		removeDeveloperButton->setEnabled(createDevelopersListWidget->currentRow() >= 0);
	}
	else
	{
		buildPackageButton->setEnabled(false);
	}
}

void MainWindow::UpdateUtilitiesInterfaceAvailability(void)
{
	if (!!(heimdallState & HeimdallState::Stopped))
	{
		detectDeviceButton->setEnabled(true);
		closePcScreenButton->setEnabled(true);
		pitSaveAsButton->setEnabled(true);

		downloadPitButton->setEnabled(!pitDestinationLineEdit->text().isEmpty());
		
		if (printPitDeviceRadioBox->isChecked())
		{
			// Device
			printLocalPitGroup->setEnabled(false);
			printPitButton->setEnabled(true);
		}
		else
		{
			// Local File
			printLocalPitGroup->setEnabled(true);
			printLocalPitLineEdit->setEnabled(true);
			printLocalPitBrowseButton->setEnabled(true);

			printPitButton->setEnabled(!printLocalPitLineEdit->text().isEmpty());
		}
	}
	else
	{
		detectDeviceButton->setEnabled(false);
		closePcScreenButton->setEnabled(false);
		pitSaveAsButton->setEnabled(false);
		downloadPitButton->setEnabled(false);

		printLocalPitGroup->setEnabled(false);
		printPitButton->setEnabled(false);
	}
}

void MainWindow::UpdateAdbCommandsInterfaceAvailability(void)
{
	bool adbAvailable = (adbProcess.state() != QProcess::Running);
	
	rebootRecoveryButton->setEnabled(adbAvailable);
	rebootDownloadButton->setEnabled(adbAvailable);
	rebootFastbootButton->setEnabled(adbAvailable);
	shutdownButton->setEnabled(adbAvailable);
	executeAdbCommandButton->setEnabled(adbAvailable && !customAdbCommandLineEdit->text().isEmpty());
	refreshDeviceInfoButton->setEnabled(adbAvailable);
	customAdbCommandLineEdit->setEnabled(adbAvailable);
	adbDevicesButton->setEnabled(adbAvailable);
	adbShellLsButton->setEnabled(adbAvailable);
	adbLogcatButton->setEnabled(adbAvailable);
	adbInstallButton->setEnabled(adbAvailable);
	clearAdbOutputButton->setEnabled(true);
}

void MainWindow::UpdateInterfaceAvailability(void)
{
	UpdateLoadPackageInterfaceAvailability();
	UpdateFlashInterfaceAvailability();
	UpdateCreatePackageInterfaceAvailability();
	UpdateUtilitiesInterfaceAvailability();
	UpdateAdbCommandsInterfaceAvailability();

	if (!!(heimdallState & HeimdallState::Stopped))
	{		
		// Enable/disable tabs

		for (int i = 0; i < functionTabWidget->count(); i++)
			functionTabWidget->setTabEnabled(i, true);

		functionTabWidget->setTabEnabled(functionTabWidget->indexOf(createPackageTab), startFlashButton->isEnabled());
	}
	else
	{
		// Disable non-current tabs

		for (int i = 0; i < functionTabWidget->count(); i++)
		{
			if (i == functionTabWidget->currentIndex())
				functionTabWidget->setTabEnabled(i, true);
			else
				functionTabWidget->setTabEnabled(i, false);
		}
	}
}

void MainWindow::UpdatePartitionNamesInterface(void)
{
	populatingPartitionNames = true;

	partitionNameComboBox->clear();

	int partitionsListWidgetRow = partitionsListWidget->currentRow();

	if (partitionsListWidgetRow >= 0)
	{
		const FileInfo& partitionInfo = workingPackageData.GetFirmwareInfo().GetFileInfos()[partitionsListWidget->currentRow()];

		for (int i = 0; i < unusedPartitionIds.length(); i++)
			partitionNameComboBox->addItem(currentPitData.FindEntry(unusedPartitionIds[i])->GetPartitionName());

		partitionNameComboBox->addItem(currentPitData.FindEntry(partitionInfo.GetPartitionId())->GetPartitionName());
		partitionNameComboBox->setCurrentIndex(unusedPartitionIds.length());
	}

	populatingPartitionNames = false;

	UpdateFlashInterfaceAvailability();
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
	setupUi(this);

	heimdallState = HeimdallState::Stopped;

	lastDirectory = QDir::toNativeSeparators(QApplication::applicationDirPath());

	populatingPartitionNames = false;

	verboseOutput = false;
	resume = false;

	tabIndex = functionTabWidget->currentIndex();
	functionTabWidget->setTabEnabled(functionTabWidget->indexOf(createPackageTab), false);

	QObject::connect(functionTabWidget, SIGNAL(currentChanged(int)), this, SLOT(FunctionTabChanged(int)));
	
	// Menu
	QObject::connect(actionDonate, SIGNAL(triggered()), this, SLOT(OpenDonationWebpage()));
	QObject::connect(actionVerboseOutput, SIGNAL(toggled(bool)), this, SLOT(SetVerboseOutput(bool)));
	QObject::connect(actionResumeConnection, SIGNAL(toggled(bool)), this, SLOT(SetResume(bool)));
	QObject::connect(actionAboutHeimdall, SIGNAL(triggered()), this, SLOT(ShowAbout()));

	// Load Package Tab
	QObject::connect(browseFirmwarePackageButton, SIGNAL(clicked()), this, SLOT(SelectFirmwarePackage()));
	QObject::connect(developerHomepageButton, SIGNAL(clicked()), this, SLOT(OpenDeveloperHomepage()));
	QObject::connect(developerDonateButton, SIGNAL(clicked()), this, SLOT(OpenDeveloperDonationWebpage()));
	QObject::connect(loadFirmwareButton, SIGNAL(clicked()), this, SLOT(LoadFirmwarePackage()));

	QObject::connect(partitionsListWidget, SIGNAL(currentRowChanged(int)), this, SLOT(SelectPartition(int)));
	QObject::connect(addPartitionButton, SIGNAL(clicked()), this, SLOT(AddPartition()));
	QObject::connect(removePartitionButton, SIGNAL(clicked()), this, SLOT(RemovePartition()));

	// Flash Tab
	QObject::connect(partitionNameComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(SelectPartitionName(int)));
	QObject::connect(partitionFileBrowseButton, SIGNAL(clicked()), this, SLOT(SelectPartitionFile()));

	QObject::connect(pitBrowseButton, SIGNAL(clicked()), this, SLOT(SelectPit()));

	QObject::connect(repartitionCheckBox, SIGNAL(stateChanged(int)), this, SLOT(SetRepartition(int)));

	QObject::connect(noRebootCheckBox, SIGNAL(stateChanged(int)), this, SLOT(SetNoReboot(int)));
	QObject::connect(resumeCheckbox, SIGNAL(stateChanged(int)), this, SLOT(SetResume(int)));
	
	QObject::connect(startFlashButton, SIGNAL(clicked()), this, SLOT(StartFlash()));

	// Create Package Tab
	QObject::connect(createFirmwareNameLineEdit, SIGNAL(textChanged(const QString&)), this, SLOT(FirmwareNameChanged(const QString&)));
	QObject::connect(createFirmwareVersionLineEdit, SIGNAL(textChanged(const QString&)), this, SLOT(FirmwareVersionChanged(const QString&)));
	QObject::connect(createPlatformNameLineEdit, SIGNAL(textChanged(const QString&)), this, SLOT(PlatformNameChanged(const QString&)));
	QObject::connect(createPlatformVersionLineEdit, SIGNAL(textChanged(const QString&)), this, SLOT(PlatformVersionChanged(const QString&)));

	QObject::connect(createHomepageLineEdit, SIGNAL(textChanged(const QString&)), this, SLOT(HomepageUrlChanged(const QString&)));
	QObject::connect(createDonateLineEdit, SIGNAL(textChanged(const QString&)), this, SLOT(DonateUrlChanged(const QString&)));

	QObject::connect(createDevelopersListWidget, SIGNAL(currentRowChanged(int)), this, SLOT(SelectDeveloper(int)));
	QObject::connect(createDeveloperNameLineEdit, SIGNAL(textChanged(const QString&)), this, SLOT(DeveloperNameChanged(const QString&)));
	QObject::connect(addDeveloperButton, SIGNAL(clicked()), this, SLOT(AddDeveloper()));
	QObject::connect(removeDeveloperButton, SIGNAL(clicked()), this, SLOT(RemoveDeveloper()));

	QObject::connect(createDevicesListWidget, SIGNAL(currentRowChanged(int)), this, SLOT(SelectDevice(int)));
	QObject::connect(deviceManufacturerLineEdit, SIGNAL(textChanged(const QString&)), this, SLOT(DeviceInfoChanged(const QString&)));
	QObject::connect(deviceNameLineEdit, SIGNAL(textChanged(const QString&)), this, SLOT(DeviceInfoChanged(const QString&)));
	QObject::connect(deviceProductCodeLineEdit, SIGNAL(textChanged(const QString&)), this, SLOT(DeviceInfoChanged(const QString&)));
	QObject::connect(addDeviceButton, SIGNAL(clicked()), this, SLOT(AddDevice()));
	QObject::connect(removeDeviceButton, SIGNAL(clicked()), this, SLOT(RemoveDevice()));
			
	QObject::connect(buildPackageButton, SIGNAL(clicked()), this, SLOT(BuildPackage()));
	QObject::connect(converterQuickButton, SIGNAL(clicked()), this, SLOT(ConvertSamsungQuick()));

	// Utilities Tab
	QObject::connect(detectDeviceButton, SIGNAL(clicked()), this, SLOT(DetectDevice()));

	QObject::connect(closePcScreenButton, SIGNAL(clicked()), this, SLOT(ClosePcScreen()));

	QObject::connect(printPitDeviceRadioBox, SIGNAL(toggled(bool)), this, SLOT(DevicePrintPitToggled(bool)));
	QObject::connect(printPitLocalFileRadioBox, SIGNAL(toggled(bool)), this, SLOT(LocalFilePrintPitToggled(bool)));
	QObject::connect(printLocalPitBrowseButton, SIGNAL(clicked()), this, SLOT(SelectPrintPitFile()));
	QObject::connect(printPitButton, SIGNAL(clicked()), this, SLOT(PrintPit()));

	QObject::connect(pitSaveAsButton, SIGNAL(clicked()), this, SLOT(SelectPitDestination()));
	QObject::connect(downloadPitButton, SIGNAL(clicked()), this, SLOT(DownloadPit()));

	// ADB Commands Tab
	QObject::connect(rebootRecoveryButton, SIGNAL(clicked()), this, SLOT(RebootToRecovery()));
	QObject::connect(rebootDownloadButton, SIGNAL(clicked()), this, SLOT(RebootToDownload()));
	QObject::connect(rebootFastbootButton, SIGNAL(clicked()), this, SLOT(RebootToFastboot()));
	QObject::connect(shutdownButton, SIGNAL(clicked()), this, SLOT(ShutdownDevice()));
	QObject::connect(executeAdbCommandButton, SIGNAL(clicked()), this, SLOT(ExecuteCustomAdbCommand()));
	QObject::connect(refreshDeviceInfoButton, SIGNAL(clicked()), this, SLOT(RefreshDeviceInfo()));
	QObject::connect(customAdbCommandLineEdit, SIGNAL(textChanged(const QString&)), this, SLOT(UpdateAdbInterface()));
	QObject::connect(adbDevicesButton, SIGNAL(clicked()), this, SLOT(ListAdbDevices()));
	QObject::connect(checkRootButton, SIGNAL(clicked()), this, SLOT(CheckRoot()));
	QObject::connect(adbShellLsButton, SIGNAL(clicked()), this, SLOT(AdbShellLs()));
	QObject::connect(adbLogcatButton, SIGNAL(clicked()), this, SLOT(AdbLogcat()));
	QObject::connect(adbInstallButton, SIGNAL(clicked()), this, SLOT(InstallApk()));
	QObject::connect(clearAdbOutputButton, SIGNAL(clicked()), this, SLOT(ClearAdbOutput()));

	// TEE Analysis Tab
	QObject::connect(teeAnalyzeButton, SIGNAL(clicked()), this, SLOT(AnalyzeTEE()));

	// Theme System - Connect menu actions
	QObject::connect(actionFollowSystem, SIGNAL(triggered()), this, SLOT(FollowSystemTheme()));
	QObject::connect(actionLightTheme, SIGNAL(triggered()), this, SLOT(LightTheme()));
	QObject::connect(actionDarkTheme, SIGNAL(triggered()), this, SLOT(DarkTheme()));

	// Heimdall Command Line
	QObject::connect(&heimdallProcess, SIGNAL(readyRead()), this, SLOT(HandleHeimdallStdout()));
	QObject::connect(&heimdallProcess, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(HandleHeimdallReturned(int, QProcess::ExitStatus)));
	QObject::connect(&heimdallProcess, SIGNAL(error(QProcess::ProcessError)), this, SLOT(HandleHeimdallError(QProcess::ProcessError)));

	// ADB Command Line  
	QObject::connect(&adbProcess, SIGNAL(readyRead()), this, SLOT(HandleAdbStdout()));
	QObject::connect(&adbProcess, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(HandleAdbReturned(int, QProcess::ExitStatus)));
	QObject::connect(&adbProcess, SIGNAL(error(QProcess::ProcessError)), this, SLOT(HandleAdbError(QProcess::ProcessError)));

	// Download Packages - init
	packageNet = new QNetworkAccessManager(this);
	downloadsDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/Heimdall";
	QDir().mkpath(downloadsDir);
	providerTemplate = "https://example.com/heimdall/{product}.json";
	providerUrlLineEdit->setText(providerTemplate);
	packagesTable->setColumnCount(5);
	QStringList hdr; hdr << "Version" << "Build" << "Date" << "Region" << "Size"; packagesTable->setHorizontalHeaderLabels(hdr);
	QObject::connect(refreshPackagesButton, SIGNAL(clicked()), this, SLOT(RefreshAvailablePackages()));
	QObject::connect(detectDeviceForPackagesButton, SIGNAL(clicked()), this, SLOT(DetectDeviceForPackages()));
	QObject::connect(downloadSelectedPackageButton, SIGNAL(clicked()), this, SLOT(DownloadSelectedPackage()));
	QObject::connect(openPackagesFolderButton, SIGNAL(clicked()), this, SLOT(OpenPackagesFolder()));

	// Initialize theme system
	currentTheme = 0; // Default to system theme
	ApplyTheme(currentTheme);
	
	// Make interface responsive by connecting to resize events
	this->installEventFilter(this);
}
// Download Packages Implementation

void MainWindow::DetectDeviceForPackages(void)
{
	// Query adb props quickly
	QProcess p;
	p.start(HeimdallFrontend::Adb::adbExecutable(), HeimdallFrontend::Adb::argsCustom("shell getprop ro.product.device ro.product.model"));
	if (!p.waitForFinished(5000)) {
		dlStatusLabel->setText("Status: ADB timeout");
		return;
	}
	QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
	QStringList lines = out.split('\n', Qt::SkipEmptyParts);
	QString product;
	QString model;
	for (const QString &line : lines) {
		if (line.contains("ro.product.device")) product = line.section(']', 1).trimmed();
		if (line.contains("ro.product.model")) model = line.section(']', 1).trimmed();
	}
	if (product.isEmpty() && !lines.isEmpty()) {
		product = lines.first().trimmed();
	}
	detectedProduct = product.isEmpty() ? QString("unknown") : product;
	deviceSummaryLabel->setText(QString("Device: %1 (%2)").arg(model.isEmpty()?"?":model, detectedProduct));
}

void MainWindow::RefreshAvailablePackages(void)
{
	packagesTable->setRowCount(0);
	QString urlTmpl = providerUrlLineEdit->text().trimmed();
	if (urlTmpl.isEmpty()) urlTmpl = providerTemplate;
	QString url = urlTmpl;
	url.replace("{product}", detectedProduct.isEmpty()?QString("unknown"):detectedProduct);
	dlStatusLabel->setText("Status: Fetching manifest...");
	QNetworkRequest req{QUrl(url)};
	req.setHeader(QNetworkRequest::UserAgentHeader, "Heimdall-Frontend");
	if (activeManifestReply) { activeManifestReply->abort(); activeManifestReply->deleteLater(); }
	activeManifestReply = packageNet->get(req);
	QObject::connect(activeManifestReply, &QNetworkReply::finished, this, &MainWindow::HandlePackageManifestFinished);
}

void MainWindow::HandlePackageManifestFinished()
{
	QScopedPointer<QNetworkReply, QScopedPointerDeleteLater> reply(activeManifestReply);
	activeManifestReply = nullptr;
	if (!reply) return;
	if (reply->error() != QNetworkReply::NoError) {
		dlStatusLabel->setText("Status: Manifest error - " + reply->errorString());
		return;
	}
	QByteArray data = reply->readAll();
	QJsonParseError jerr;
	QJsonDocument doc = QJsonDocument::fromJson(data, &jerr);
	if (jerr.error != QJsonParseError::NoError || !doc.isArray()) {
		dlStatusLabel->setText("Status: Invalid manifest JSON");
		return;
	}
	QJsonArray arr = doc.array();
	packagesTable->setRowCount(arr.size());
	int row = 0;
	for (const QJsonValue &v : arr) {
		QJsonObject o = v.toObject();
		auto setItem = [&](int c, const QString &text){
			QTableWidgetItem *it = new QTableWidgetItem(text);
			it->setFlags(it->flags() ^ Qt::ItemIsEditable);
			packagesTable->setItem(row, c, it);
		};
		setItem(0, o.value("version").toString());
		setItem(1, o.value("build").toString());
		setItem(2, o.value("date").toString());
		setItem(3, o.value("region").toString());
		setItem(4, o.value("size").toString());
		// store download url in first column data
		if (packagesTable->item(row,0)) {
			packagesTable->item(row,0)->setData(Qt::UserRole, o.value("url").toString());
		}
		row++;
	}
	dlStatusLabel->setText(QString("Status: %1 package(s) listed").arg(arr.size()));
}

void MainWindow::DownloadSelectedPackage(void)
{
	int row = packagesTable->currentRow();
	if (row < 0) { dlStatusLabel->setText("Status: Select a package"); return; }
	QString url = packagesTable->item(row,0)->data(Qt::UserRole).toString();
	if (url.isEmpty()) { dlStatusLabel->setText("Status: Missing URL"); return; }
	QNetworkRequest req{QUrl(url)};
	req.setHeader(QNetworkRequest::UserAgentHeader, "Heimdall-Frontend");
	if (activeDownloadReply) { activeDownloadReply->abort(); activeDownloadReply->deleteLater(); }
	activeDownloadReply = packageNet->get(req);
	QObject::connect(activeDownloadReply, &QNetworkReply::downloadProgress, this, &MainWindow::HandlePackageDownloadProgress);
	QObject::connect(activeDownloadReply, &QNetworkReply::finished, this, &MainWindow::HandlePackageDownloadFinished);
	dlStatusLabel->setText("Status: Downloading...");
}

void MainWindow::HandlePackageDownloadProgress(qint64 received, qint64 total)
{
	if (total > 0) dlStatusLabel->setText(QString("Status: Downloading %1% ").arg((int)(received*100/total)));
}

void MainWindow::HandlePackageDownloadFinished()
{
	QScopedPointer<QNetworkReply, QScopedPointerDeleteLater> reply(activeDownloadReply);
	activeDownloadReply = nullptr;
	if (!reply) return;
	if (reply->error() != QNetworkReply::NoError) {
		dlStatusLabel->setText("Status: Download error - " + reply->errorString());
		return;
	}
	// Determine filename
	QUrl url = reply->url();
	QString base = QFileInfo(url.path()).fileName();
	if (base.isEmpty()) base = "package.bin";
	QString path = downloadsDir + "/" + base;
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly)) {
		dlStatusLabel->setText("Status: Cannot write file");
		return;
	}
	f.write(reply->readAll());
	f.close();
	dlStatusLabel->setText("Status: Downloaded -> " + path);
}

void MainWindow::OpenPackagesFolder(void)
{
	QDesktopServices::openUrl(QUrl::fromLocalFile(downloadsDir));
}
// TEE Analysis Implementation

void MainWindow::AnalyzeTEE(void)
{
	teeTypeLabel->setText("TEE: Analyzing...");
	teeOutputTextEdit->clear();

	// Run ADB commands synchronously to gather evidence
	auto runAdb = [](const QStringList& args) -> QString {
		QProcess p;
		p.start(HeimdallFrontend::Adb::adbExecutable(), args);
		bool ok = p.waitForFinished(8000);
		QString out = QString::fromUtf8(p.readAllStandardOutput());
		QString err = QString::fromUtf8(p.readAllStandardError());
		return ok ? out : (out + "\n" + err);
	};

	QString props = runAdb(HeimdallFrontend::Adb::argsCustom("shell getprop"));
	QString devNodes = runAdb(HeimdallFrontend::Adb::argsCustom("shell ls -la /dev"));
	QString kernelLog = runAdb(QStringList() << "logcat" << "-b" << "kernel" << "-d");
	QString vendorLibs64 = runAdb(HeimdallFrontend::Adb::argsCustom("shell ls /vendor/lib64"));
	QString vendorLibs32 = runAdb(HeimdallFrontend::Adb::argsCustom("shell ls /vendor/lib"));

	QStringList libs;
	for (const auto& line : vendorLibs64.split('\n', Qt::SkipEmptyParts)) libs << line.trimmed();
	for (const auto& line : vendorLibs32.split('\n', Qt::SkipEmptyParts)) libs << line.trimmed();

	auto result = HeimdallFrontend::TEE::analyze(props, devNodes, kernelLog, libs);

	teeTypeLabel->setText(QString("TEE: %1 (confidence %2%)").arg(result.typeName).arg(result.confidence));

	teeOutputTextEdit->append("<b>Indicators matched:</b>");
	for (const auto& h : result.indicators)
		teeOutputTextEdit->append("• " + h);

	teeOutputTextEdit->append("\n<b>Sample props:</b>\n" + props.left(2000));
	teeOutputTextEdit->append("\n<b>Kernel log (filtered):</b>");
	QString filteredKernel;
	for (const auto& line : kernelLog.split('\n')) {
		if (line.contains("tee", Qt::CaseInsensitive) || line.contains("qsee", Qt::CaseInsensitive) || line.contains("trust", Qt::CaseInsensitive))
			filteredKernel += line + "\n";
	}
	teeOutputTextEdit->append(filteredKernel.isEmpty() ? QString("(no tee-related kernel lines found)") : filteredKernel);
}

MainWindow::~MainWindow()
{
}

void MainWindow::OpenDonationWebpage(void)
{
	QDesktopServices::openUrl(QUrl("http://www.glassechidna.com.au/donate/", QUrl::StrictMode));
}

void MainWindow::SetVerboseOutput(bool enabled)
{
	verboseOutput = enabled;
}

void MainWindow::ShowAbout(void)
{
	aboutForm.show();
}

void MainWindow::FunctionTabChanged(int index)
{
	tabIndex = index;
	deviceDetectedRadioButton->setChecked(false);
}

void MainWindow::SelectFirmwarePackage(void)
{
	loadedPackageData.Clear();
	UpdatePackageUserInterface();

	QString path = PromptFileSelection("Select Package", "Firmware Package (*.gz)");
	firmwarePackageLineEdit->setText(path);

	if (firmwarePackageLineEdit->text() != "")
	{
		if (Packaging::ExtractPackage(firmwarePackageLineEdit->text(), &loadedPackageData))
			UpdatePackageUserInterface();
		else
			loadedPackageData.Clear();
	}
}

void MainWindow::OpenDeveloperHomepage(void)
{
	if(!QDesktopServices::openUrl(QUrl(loadedPackageData.GetFirmwareInfo().GetUrl(), QUrl::TolerantMode)))
		Alerts::DisplayWarning(QString("Cannot open invalid URL:\n%1").arg(loadedPackageData.GetFirmwareInfo().GetUrl()));
}

void MainWindow::OpenDeveloperDonationWebpage(void)
{
	if (!QDesktopServices::openUrl(QUrl(loadedPackageData.GetFirmwareInfo().GetDonateUrl(), QUrl::TolerantMode)))
		Alerts::DisplayWarning(QString("Cannot open invalid URL:\n%1").arg(loadedPackageData.GetFirmwareInfo().GetDonateUrl()));
}

void MainWindow::LoadFirmwarePackage(void)
{
	workingPackageData.Clear();
	currentPitData.Clear();
	
	workingPackageData.GetFiles().append(loadedPackageData.GetFiles());
	loadedPackageData.RemoveAllFiles();

	const QList<FileInfo> packageFileInfos = loadedPackageData.GetFirmwareInfo().GetFileInfos();

	for (int i = 0; i < packageFileInfos.length(); i++)
	{
		bool fileFound = false;

		for (int j = 0; j < workingPackageData.GetFiles().length(); j++)
		{
			if (workingPackageData.GetFiles()[j]->fileTemplate() == ("XXXXXX-" + packageFileInfos[i].GetFilename()))
			{
				FileInfo partitionInfo(packageFileInfos[i].GetPartitionId(), QDir::current().absoluteFilePath(workingPackageData.GetFiles()[j]->fileName()));
				workingPackageData.GetFirmwareInfo().GetFileInfos().append(partitionInfo);

				fileFound = true;
				break;
			}
		}

		if (!fileFound)
			Alerts::DisplayWarning(QString("%1 is missing from the package.").arg(packageFileInfos[i].GetFilename()));
	}

	// Find the PIT file and read it
	for (int i = 0; i < workingPackageData.GetFiles().length(); i++)
	{
		QTemporaryFile *file = workingPackageData.GetFiles()[i];

		if (file->fileTemplate() == ("XXXXXX-" + loadedPackageData.GetFirmwareInfo().GetPitFilename()))
		{
			workingPackageData.GetFirmwareInfo().SetPitFilename(QDir::current().absoluteFilePath(file->fileName()));

			if (!ReadPit(file))
			{
				Alerts::DisplayError("Failed to read PIT file.");

				loadedPackageData.Clear();
				UpdatePackageUserInterface();

				workingPackageData.Clear();
				UpdateUnusedPartitionIds();
				return;
			}

			break;
		}
	}

	UpdateUnusedPartitionIds();
	workingPackageData.GetFirmwareInfo().SetRepartition(loadedPackageData.GetFirmwareInfo().GetRepartition());
	workingPackageData.GetFirmwareInfo().SetNoReboot(loadedPackageData.GetFirmwareInfo().GetNoReboot());

	loadedPackageData.Clear();
	UpdatePackageUserInterface();
	firmwarePackageLineEdit->clear();

	partitionsListWidget->clear();

	// Populate partitionsListWidget with partition names (from the PIT file)
	for (int i = 0; i < workingPackageData.GetFirmwareInfo().GetFileInfos().length(); i++)
	{
		const FileInfo& partitionInfo = workingPackageData.GetFirmwareInfo().GetFileInfos()[i];

		const PitEntry *pitEntry = currentPitData.FindEntry(partitionInfo.GetPartitionId());

		if (pitEntry)
		{
			partitionsListWidget->addItem(pitEntry->GetPartitionName());
		}
		else
		{
			Alerts::DisplayError("Firmware package includes invalid partition IDs.");

			loadedPackageData.GetFirmwareInfo().Clear();
			currentPitData.Clear();
			UpdateUnusedPartitionIds();

			partitionsListWidget->clear();
			return;
		}
	}

	partitionNameComboBox->clear();
	partitionIdLineEdit->clear();
	partitionFileLineEdit->clear();
	partitionFileBrowseButton->setEnabled(false);

	repartitionCheckBox->setEnabled(true);
	repartitionCheckBox->setChecked(workingPackageData.GetFirmwareInfo().GetRepartition());
	noRebootCheckBox->setEnabled(true);
	noRebootCheckBox->setChecked(workingPackageData.GetFirmwareInfo().GetNoReboot());

	partitionsListWidget->setEnabled(true);
	addPartitionButton->setEnabled(true);
	removePartitionButton->setEnabled(partitionsListWidget->currentRow() >= 0);

	pitLineEdit->setText(workingPackageData.GetFirmwareInfo().GetPitFilename());

	functionTabWidget->setCurrentWidget(flashTab);

	UpdateInterfaceAvailability();
}

void MainWindow::SelectPartitionName(int index)
{
	if (!populatingPartitionNames && index != -1 && index != unusedPartitionIds.length())
	{
		unsigned int newPartitionIndex = unusedPartitionIds[index];
		unusedPartitionIds.removeAt(index);

		FileInfo& fileInfo = workingPackageData.GetFirmwareInfo().GetFileInfos()[partitionsListWidget->currentRow()];
		unusedPartitionIds.append(fileInfo.GetPartitionId());
		fileInfo.SetPartitionId(newPartitionIndex);

		PitEntry *pitEntry = currentPitData.FindEntry(newPartitionIndex);

		QString title("File");

		if (pitEntry && strlen(pitEntry->GetFlashFilename()) > 0)
			title += " (" + QString(pitEntry->GetFlashFilename()) + ")";

		partitionFileGroup->setTitle(title);

		if (pitEntry && !fileInfo.GetFilename().isEmpty())
		{
			QString partitionFilename = pitEntry->GetFlashFilename();
			int lastPeriod = partitionFilename.lastIndexOf(QChar('.'));

			if (lastPeriod >= 0)
			{
				QString partitionFileExtension = partitionFilename.mid(lastPeriod + 1);

				lastPeriod = fileInfo.GetFilename().lastIndexOf(QChar('.'));

				if (lastPeriod < 0 || fileInfo.GetFilename().mid(lastPeriod + 1) != partitionFileExtension)
					Alerts::DisplayWarning(QString("%1 partition expects files with file extension \"%2\".").arg(pitEntry->GetPartitionName(), partitionFileExtension));
			}
		}

		partitionNameComboBox->clear();

		// Update interface
		UpdatePartitionNamesInterface();
		partitionIdLineEdit->setText(QString::number(newPartitionIndex));
		partitionsListWidget->currentItem()->setText(currentPitData.FindEntry(newPartitionIndex)->GetPartitionName());
	}
}

void MainWindow::SelectPartitionFile(void)
{
	QString path = PromptFileSelection();

	if (path != "")
	{
		FileInfo& fileInfo = workingPackageData.GetFirmwareInfo().GetFileInfos()[partitionsListWidget->currentRow()];
		PitEntry *pitEntry = currentPitData.FindEntry(fileInfo.GetPartitionId());

		QString partitionFilename = pitEntry->GetFlashFilename();
		int lastPeriod = partitionFilename.lastIndexOf(QChar('.'));

		if (lastPeriod >= 0)
		{
			QString partitionFileExtension = partitionFilename.mid(lastPeriod + 1);

			lastPeriod = path.lastIndexOf(QChar('.'));

			if (lastPeriod < 0 || path.mid(lastPeriod + 1) != partitionFileExtension)
				Alerts::DisplayWarning(QString("%1 partition expects files with file extension \"%2\".").arg(pitEntry->GetPartitionName(), partitionFileExtension));
		}

		fileInfo.SetFilename(path);
		partitionFileLineEdit->setText(path);

		pitBrowseButton->setEnabled(true);
		partitionsListWidget->setEnabled(true);
		UpdateInterfaceAvailability();

		if (unusedPartitionIds.length() > 0)
			addPartitionButton->setEnabled(true);
	}
}

void MainWindow::SelectPartition(int row)
{
	if (row >= 0)
	{
		const FileInfo& partitionInfo = workingPackageData.GetFirmwareInfo().GetFileInfos()[row];

		UpdatePartitionNamesInterface();

		partitionIdLineEdit->setText(QString::number(partitionInfo.GetPartitionId()));
		partitionFileLineEdit->setText(partitionInfo.GetFilename());
		partitionFileBrowseButton->setEnabled(true);

		removePartitionButton->setEnabled(true);

		QString title("File");

		PitEntry *pitEntry = currentPitData.FindEntry(partitionInfo.GetPartitionId());

		if (pitEntry && strlen(pitEntry->GetFlashFilename()) > 0)
			title += " (" + QString(pitEntry->GetFlashFilename()) + ")";

		partitionFileGroup->setTitle(title);
	}
	else
	{
		UpdatePartitionNamesInterface();

		partitionIdLineEdit->clear();
		partitionFileLineEdit->clear();
		partitionFileBrowseButton->setEnabled(false);

		removePartitionButton->setEnabled(false);

		partitionFileGroup->setTitle("File");
	}
}

void MainWindow::AddPartition(void)
{
	FileInfo partitionInfo(unusedPartitionIds.first(), "");
	workingPackageData.GetFirmwareInfo().GetFileInfos().append(partitionInfo);
	UpdateUnusedPartitionIds();

	pitBrowseButton->setEnabled(false);
	addPartitionButton->setEnabled(false);

	partitionsListWidget->addItem(currentPitData.FindEntry(partitionInfo.GetPartitionId())->GetPartitionName());
	partitionsListWidget->setCurrentRow(partitionsListWidget->count() - 1);
	partitionsListWidget->setEnabled(false);

	UpdateInterfaceAvailability();
}

void MainWindow::RemovePartition(void)
{
	workingPackageData.GetFirmwareInfo().GetFileInfos().removeAt(partitionsListWidget->currentRow());
	UpdateUnusedPartitionIds();

	QListWidgetItem *item = partitionsListWidget->currentItem();
	partitionsListWidget->setCurrentRow(-1);
	delete item;

	pitBrowseButton->setEnabled(true);
	addPartitionButton->setEnabled(true);
	partitionsListWidget->setEnabled(true);
	UpdateInterfaceAvailability();
}

void MainWindow::SelectPit(void)
{
	QString path = PromptFileSelection("Select PIT", "*.pit");
	bool validPit = path != "";

	if (validPit)
	{
		// In order to map files in the old PIT to file in the new one, we first must use partition names instead of IDs.
		QList<FileInfo> fileInfos = workingPackageData.GetFirmwareInfo().GetFileInfos();

		int partitionNamesCount = fileInfos.length();
		QString *partitionNames = new QString[fileInfos.length()];
		for (int i = 0; i < fileInfos.length(); i++)
			partitionNames[i] = currentPitData.FindEntry(fileInfos[i].GetPartitionId())->GetPartitionName();

		currentPitData.Clear();

		QFile pitFile(path);

		if (ReadPit(&pitFile))
		{
			workingPackageData.GetFirmwareInfo().SetPitFilename(path);

			partitionsListWidget->clear();
			int partitionInfoIndex = 0;

			for (int i = 0; i < partitionNamesCount; i++)
			{
				const PitEntry *pitEntry = currentPitData.FindEntry(partitionNames[i].toLatin1().constData());
				
				if (pitEntry)
				{
					fileInfos[partitionInfoIndex++].SetPartitionId(pitEntry->GetIdentifier());
					partitionsListWidget->addItem(pitEntry->GetPartitionName());
				}
				else
				{
					fileInfos.removeAt(partitionInfoIndex);
				}
			}
		}
		else
		{
			validPit = false;
		}

		// If the selected PIT was invalid, attempt to reload the old one.
		if (!validPit)
		{
			Alerts::DisplayError("The file selected was not a valid PIT file.");

			if (!workingPackageData.GetFirmwareInfo().GetPitFilename().isEmpty())
			{
				QFile originalPitFile(workingPackageData.GetFirmwareInfo().GetPitFilename());

				if (ReadPit(&originalPitFile))
				{
					validPit = true;
				}
				else
				{
					Alerts::DisplayError("Failed to reload working PIT data.");

					workingPackageData.Clear();
					partitionsListWidget->clear();
				}
			}
		}

		UpdateUnusedPartitionIds();

		delete [] partitionNames;

		pitLineEdit->setText(workingPackageData.GetFirmwareInfo().GetPitFilename());

		repartitionCheckBox->setEnabled(validPit);
		noRebootCheckBox->setEnabled(validPit);
		partitionsListWidget->setEnabled(validPit);

		addPartitionButton->setEnabled(validPit);
		removePartitionButton->setEnabled(validPit && partitionsListWidget->currentRow() >= 0);

		UpdateInterfaceAvailability();
	}
}


void MainWindow::SetRepartition(int enabled)
{
	workingPackageData.GetFirmwareInfo().SetRepartition(enabled);

	repartitionCheckBox->setChecked(enabled);
}

void MainWindow::SetNoReboot(int enabled)
{
	workingPackageData.GetFirmwareInfo().SetNoReboot(enabled);

	noRebootCheckBox->setChecked(enabled);
}

void MainWindow::SetResume(bool enabled)
{
	resume = enabled;

	actionResumeConnection->setChecked(enabled);
	resumeCheckbox->setChecked(enabled);
}

void MainWindow::SetResume(int enabled)
{
	SetResume(enabled != 0);
}

void MainWindow::StartFlash(void)
{
	outputPlainTextEdit->clear();

	heimdallState = HeimdallState::Flashing;
	heimdallFailed = false;

	const FirmwareInfo& firmwareInfo = workingPackageData.GetFirmwareInfo();
	const QList<FileInfo>& fileInfos = firmwareInfo.GetFileInfos();

	bool singlePartitionFlash = (fileInfos.length() == 1);
	
	QStringList arguments;
	arguments.append("flash");

	// Only allow repartition if flashing multiple partitions
	if (firmwareInfo.GetRepartition() && !singlePartitionFlash)
		arguments.append("--repartition");
	else if (firmwareInfo.GetRepartition() && singlePartitionFlash)
	{
		// Inform user we’re skipping repartition for single-partition flash
		flashLabel->setText("Skipping repartition (single partition flash)");
	}

	// Use uppercase flag for PIT as per Heimdall conventions
	arguments.append("--PIT");
	arguments.append(firmwareInfo.GetPitFilename());

	for (int i = 0; i < fileInfos.length(); i++)
	{
		// Prefer partition name flags (e.g., --RECOVERY) over numeric IDs
		const PitEntry *pe = currentPitData.FindEntry(fileInfos[i].GetPartitionId());
		QString flag;
		if (pe && pe->IsFlashable())
		{
			flag = QString("--") + QString::fromLatin1(pe->GetPartitionName());
		}
		else
		{
			flag.sprintf("--%u", fileInfos[i].GetPartitionId());
		}

		arguments.append(flag);
		arguments.append(fileInfos[i].GetFilename());
	}

	if (firmwareInfo.GetNoReboot())
	{
		arguments.append("--no-reboot");
		heimdallState |= HeimdallState::NoReboot;
	}

	if (resume)
		arguments.append("--resume");

	if (verboseOutput)
		arguments.append("--verbose");

	arguments.append("--stdout-errors");

	StartHeimdall(arguments);
}

void MainWindow::FirmwareNameChanged(const QString& text)
{
	workingPackageData.GetFirmwareInfo().SetName(text);
	UpdateInterfaceAvailability();
}

void MainWindow::FirmwareVersionChanged(const QString& text)
{
	workingPackageData.GetFirmwareInfo().SetVersion(text);
	UpdateInterfaceAvailability();
}

void MainWindow::PlatformNameChanged(const QString& text)
{
	workingPackageData.GetFirmwareInfo().GetPlatformInfo().SetName(text);
	UpdateInterfaceAvailability();
}

void MainWindow::PlatformVersionChanged(const QString& text)
{
	workingPackageData.GetFirmwareInfo().GetPlatformInfo().SetVersion(text);
	UpdateInterfaceAvailability();
}

void MainWindow::HomepageUrlChanged(const QString& text)
{
	workingPackageData.GetFirmwareInfo().SetUrl(text);
}

void MainWindow::DonateUrlChanged(const QString& text)
{
	workingPackageData.GetFirmwareInfo().SetDonateUrl(text);
}

void MainWindow::DeveloperNameChanged(const QString& text)
{
	UNUSED(text);

	UpdateCreatePackageInterfaceAvailability();
}

void MainWindow::SelectDeveloper(int row)
{
	UNUSED(row);

	UpdateCreatePackageInterfaceAvailability();
}

void MainWindow::AddDeveloper(void)
{
	workingPackageData.GetFirmwareInfo().GetDevelopers().append(createDeveloperNameLineEdit->text());

	createDevelopersListWidget->addItem(createDeveloperNameLineEdit->text());
	createDeveloperNameLineEdit->clear();
	
	UpdateCreatePackageInterfaceAvailability();
}

void MainWindow::RemoveDeveloper(void)
{
	workingPackageData.GetFirmwareInfo().GetDevelopers().removeAt(createDevelopersListWidget->currentRow());

	QListWidgetItem *item = createDevelopersListWidget->currentItem();
	createDevelopersListWidget->setCurrentRow(-1);
	delete item;

	removeDeveloperButton->setEnabled(false);
	
	UpdateInterfaceAvailability();
}

void MainWindow::DeviceInfoChanged(const QString& text)
{
	UNUSED(text);

	if (deviceManufacturerLineEdit->text().isEmpty() || deviceNameLineEdit->text().isEmpty() || deviceProductCodeLineEdit->text().isEmpty())
		addDeviceButton->setEnabled(false);
	else
		addDeviceButton->setEnabled(true);
}

void MainWindow::SelectDevice(int row)
{
	removeDeviceButton->setEnabled(row >= 0);
}

void MainWindow::AddDevice(void)
{
	workingPackageData.GetFirmwareInfo().GetDeviceInfos().append(DeviceInfo(deviceManufacturerLineEdit->text(), deviceProductCodeLineEdit->text(),
		deviceNameLineEdit->text()));

	createDevicesListWidget->addItem(deviceManufacturerLineEdit->text() + " " + deviceNameLineEdit->text() + ": " + deviceProductCodeLineEdit->text());
	deviceManufacturerLineEdit->clear();
	deviceNameLineEdit->clear();
	deviceProductCodeLineEdit->clear();
	
	UpdateInterfaceAvailability();
}

void MainWindow::RemoveDevice(void)
{
	workingPackageData.GetFirmwareInfo().GetDeviceInfos().removeAt(createDevicesListWidget->currentRow());

	QListWidgetItem *item = createDevicesListWidget->currentItem();
	createDevicesListWidget->setCurrentRow(-1);
	delete item;

	removeDeviceButton->setEnabled(false);
	
	UpdateInterfaceAvailability();
}
			
void MainWindow::BuildPackage(void)
{
	QString packagePath = PromptFileCreation("Save Package", "Firmware Package (*.gz)");

	if (!packagePath.isEmpty())
	{
		if (!packagePath.endsWith(".tar.gz", Qt::CaseInsensitive))
		{
			if (packagePath.endsWith(".tar", Qt::CaseInsensitive))
				packagePath.append(".gz");
			else if (packagePath.endsWith(".gz", Qt::CaseInsensitive))
				packagePath.replace(packagePath.length() - 3, ".tar.gz");
			else if (packagePath.endsWith(".tgz", Qt::CaseInsensitive))
				packagePath.replace(packagePath.length() - 4, ".tar.gz");
			else
				packagePath.append(".tar.gz");
		}

		Packaging::BuildPackage(packagePath, workingPackageData.GetFirmwareInfo());
	}
}

static QString basenameLower(const QString &path)
{
	QString base = QFileInfo(path).fileName();
	return base.toLower();
}

static QString stripExtensions(const QString &name)
{
	QString s = name;
	int idx;
	// Remove multiple extensions like .img.lz4 or .tar.md5
	for (int i = 0; i < 2; ++i) {
		idx = s.lastIndexOf('.');
		if (idx > 0) s = s.left(idx);
	}
	return s;
}

void MainWindow::ConvertSamsungQuick(void)
{
	QString pitPath = PromptFileSelection("Select PIT", "*.pit");
	if (pitPath.isEmpty()) return;

	QFile pitFile(pitPath);
	if (!ReadPit(&pitFile))
	{
		Alerts::DisplayError("Failed to read PIT file. Please select a valid PIT.");
		return;
	}

	QStringList srcFiles = QFileDialog::getOpenFileNames(this, "Select Samsung firmware files",
		lastDirectory, "Firmware Files (*.img *.img.lz4 *.bin *.mbn *.elf *.tar *.md5);;All Files (*)");

	if (srcFiles.isEmpty()) return;

	QStringList skippedArchives;
	QList<FileInfo> mapped;

	auto findPartitionIdByName = [&](const QString &cand) -> int {
		if (cand.isEmpty()) return -1;
		const PitEntry *e = currentPitData.FindEntry(cand.toUtf8().constData());
		if (e) return (int)e->GetIdentifier();
		return -1;
	};

	auto findPartitionIdByFlashFilename = [&](const QString &fileBase) -> int {
		for (unsigned int i = 0; i < currentPitData.GetEntryCount(); i++)
		{
			const PitEntry *pe = currentPitData.GetEntry(i);
			if (!pe->IsFlashable()) continue;
			QString flash = QString::fromLatin1(pe->GetFlashFilename()).toLower();
			if (flash.isEmpty()) continue;
			if (flash == fileBase || flash.contains(fileBase)) return (int)pe->GetIdentifier();
		}
		return -1;
	};

	auto tryCandidates = [&](const QStringList &cands, const QString &fileBase) -> int {
		for (const QString &c : cands)
		{
			int id = findPartitionIdByName(c);
			if (id >= 0) return id;
		}
		return findPartitionIdByFlashFilename(fileBase);
	};

	for (const QString &path : srcFiles)
	{
		QString lower = basenameLower(path);
		if (lower.endsWith(".tar") || lower.endsWith(".md5"))
		{
			skippedArchives << lower;
			continue;
		}

		QString baseNoExt = stripExtensions(lower);

		QStringList cands;

		if (lower.contains("home_csc")) { cands << "CSC" << "ODM" << "OMC"; }
		if (lower.contains("csc")) { cands << "CSC" << "ODM" << "OMC"; }
		if (lower.contains("modem") || lower.startsWith("cp_")) { cands << "MODEM" << "CP"; }
		if (lower.contains("bootloader") || lower.contains("sboot")) { cands << "SBOOT" << "BOOTLOADER"; }
		if (lower.contains("boot") && !lower.contains("bootloader")) { cands << "BOOT"; }
		if (lower.contains("recovery")) { cands << "RECOVERY"; }
		if (lower.contains("system")) { cands << "SYSTEM"; }
		if (lower.contains("vendor")) { cands << "VENDOR"; }
		if (lower.contains("product")) { cands << "PRODUCT"; }
		if (lower.contains("userdata")) { cands << "USERDATA"; }
		if (lower.contains("cache")) { cands << "CACHE"; }
		if (lower.contains("dtbo")) { cands << "DTBO"; }
		if (lower.contains("vbmeta")) { cands << "VBMETA_SYSTEM" << "VBMETA_VENDOR" << "VBMETA"; }
		if (lower.contains("param")) { cands << "PARAM"; }
		if (lower == "cm.bin" || lower.contains("cm")) { cands << "CM"; }

		// Fallback: also try uppercase of file basename as direct partition name
		cands << stripExtensions(lower).toUpper();

		int partId = tryCandidates(cands, baseNoExt);
		if (partId >= 0)
		{
			mapped.append(FileInfo((unsigned int)partId, path));
		}
	}

	if (mapped.isEmpty())
	{
		QString msg = "No files could be mapped. Ensure you select extracted images (not .tar/.md5).";
		if (!skippedArchives.isEmpty()) msg += "\nSkipped archives: " + skippedArchives.join(", ");
		Alerts::DisplayError(msg);
		return;
	}

	FirmwareInfo fi;
	fi.SetName("Samsung Conversion");
	fi.SetVersion(QDateTime::currentDateTime().toString("yyyyMMdd-HHmm"));
	fi.GetPlatformInfo().SetName("Android");
	fi.GetPlatformInfo().SetVersion("");
	fi.SetPitFilename(pitPath);
	fi.SetRepartition(false);
	fi.SetNoReboot(false);
	for (const FileInfo &f : mapped) fi.GetFileInfos().append(f);

	QString outPath = PromptFileCreation("Save Package", "Firmware Package (*.gz)");
	if (outPath.isEmpty()) return;

	if (!outPath.endsWith(".tar.gz", Qt::CaseInsensitive))
	{
		if (outPath.endsWith(".tar", Qt::CaseInsensitive)) outPath.append(".gz");
		else if (outPath.endsWith(".gz", Qt::CaseInsensitive)) outPath.replace(outPath.length() - 3, 3, ".tar.gz");
		else if (outPath.endsWith(".tgz", Qt::CaseInsensitive)) outPath.replace(outPath.length() - 4, 4, ".tar.gz");
		else outPath.append(".tar.gz");
	}

	if (!Packaging::BuildPackage(outPath, fi))
	{
		Alerts::DisplayError("Failed to build Heimdall package.");
		return;
	}

	Alerts::DisplayWarning(QString("Package created:\n%1").arg(outPath));
}

void MainWindow::DetectDevice(void)
{
	deviceDetectedRadioButton->setChecked(false);
	utilityOutputPlainTextEdit->clear();

	heimdallState = HeimdallState::DetectingDevice;
	heimdallFailed = false;
	
	QStringList arguments;
	arguments.append("detect");

	if (verboseOutput)
		arguments.append("--verbose");

	arguments.append("--stdout-errors");

	StartHeimdall(arguments);
}

void MainWindow::ClosePcScreen(void)
{
	utilityOutputPlainTextEdit->clear();

	heimdallState = HeimdallState::ClosingPcScreen;
	heimdallFailed = false;
	
	QStringList arguments;
	arguments.append("close-pc-screen");
	
	if (resume)
		arguments.append("--resume");

	if (verboseOutput)
		arguments.append("--verbose");

	arguments.append("--stdout-errors");

	StartHeimdall(arguments);
}

void MainWindow::SelectPitDestination(void)
{
	QString path = PromptFileCreation("Save PIT", "*.pit");

	if (path != "")
	{
		if (!path.endsWith(".pit"))
			path.append(".pit");

		pitDestinationLineEdit->setText(path);

		UpdateInterfaceAvailability();
	}
}

void MainWindow::DownloadPit(void)
{
	deviceDetectedRadioButton->setChecked(false);
	utilityOutputPlainTextEdit->clear();

	heimdallState = HeimdallState::DownloadingPit | HeimdallState::NoReboot;
	heimdallFailed = false;
	
	QStringList arguments;
	arguments.append("download-pit");

	arguments.append("--output");
	arguments.append(pitDestinationLineEdit->text());

	arguments.append("--no-reboot");

	if (resume)
		arguments.append("--resume");

	if (verboseOutput)
		arguments.append("--verbose");

	arguments.append("--stdout-errors");

	StartHeimdall(arguments);
}

void MainWindow::DevicePrintPitToggled(bool checked)
{
	if (checked)
	{
		if (printPitLocalFileRadioBox->isChecked())
			printPitLocalFileRadioBox->setChecked(false);
	}

	UpdateUtilitiesInterfaceAvailability();
}

void MainWindow::LocalFilePrintPitToggled(bool checked)
{
	if (checked)
	{
		if (printPitDeviceRadioBox->isChecked())
			printPitDeviceRadioBox->setChecked(false);
	}

	UpdateUtilitiesInterfaceAvailability();
}

void MainWindow::SelectPrintPitFile(void)
{
	QString path = PromptFileSelection("Select PIT", "*.pit");

	if (path.length() > 0)
	{
		printLocalPitLineEdit->setText(path);
		printPitButton->setEnabled(true);
	}
	else
	{
		printPitButton->setEnabled(false);
	}
}

void MainWindow::PrintPit(void)
{
	utilityOutputPlainTextEdit->clear();

	heimdallState = HeimdallState::PrintingPit | HeimdallState::NoReboot;
	heimdallFailed = false;
	
	QStringList arguments;
	arguments.append("print-pit");

	if (printPitLocalFileRadioBox->isChecked())
	{
		arguments.append("--file");
		arguments.append(printLocalPitLineEdit->text());
	}

	arguments.append("--stdout-errors");
	arguments.append("--no-reboot");
	
	if (resume)
		arguments.append("--resume");

	if (verboseOutput)
		arguments.append("--verbose");

	StartHeimdall(arguments);
}

void MainWindow::HandleHeimdallStdout(void)
{
	QString output = heimdallProcess.readAll();

	// We often receive multiple lots of output from Heimdall at one time. So we use regular expressions
	// to ensure we don't miss out on any important information.
	QRegExp uploadingExp("Uploading [^\n]+\n");
	if (output.lastIndexOf(uploadingExp) > -1)
		flashLabel->setText(uploadingExp.cap().left(uploadingExp.cap().length() - 1));

	QRegExp percentExp("[\b\n][0-9]+%");
	if (output.lastIndexOf(percentExp) > -1)
	{
		QString percentString = percentExp.cap();
		flashProgressBar->setValue(percentString.mid(1, percentString.length() - 2).toInt());
	}

	output.remove(QChar('\b'));
	output.replace(QChar('%'), QString("%\n"));

	if (!!(heimdallState & HeimdallState::Flashing))
	{
		outputPlainTextEdit->insertPlainText(output);
		outputPlainTextEdit->ensureCursorVisible();
	}
	else
	{
		utilityOutputPlainTextEdit->insertPlainText(output);
		utilityOutputPlainTextEdit->ensureCursorVisible();
	}
}

void MainWindow::HandleHeimdallReturned(int exitCode, QProcess::ExitStatus exitStatus)
{
	HandleHeimdallStdout();

	if (exitStatus == QProcess::NormalExit && exitCode == 0)
	{
		SetResume(!!(heimdallState & HeimdallState::NoReboot));

		if (!!(heimdallState & HeimdallState::Flashing))
		{
			flashLabel->setText("Flash completed successfully!");
		}
		else if (!!(heimdallState & HeimdallState::DetectingDevice))
		{
			deviceDetectedRadioButton->setChecked(true);
		}
	}
	else
	{
		if (!!(heimdallState & HeimdallState::Flashing))
		{
			QString error = heimdallProcess.readAllStandardError();

			int lastNewLineChar = error.lastIndexOf('\n');

			if (lastNewLineChar == 0)
				error = error.mid(1).remove("ERROR: ");
			else
				error = error.left(lastNewLineChar).remove("ERROR: ");

			flashLabel->setText(error);
		}
		else if (!!(heimdallState & HeimdallState::DetectingDevice))
		{
			deviceDetectedRadioButton->setChecked(false);
		}
	}

	heimdallState = HeimdallState::Stopped;
	flashProgressBar->setValue(0);
	flashProgressBar->setEnabled(false);
	UpdateInterfaceAvailability();
}

void MainWindow::HandleHeimdallError(QProcess::ProcessError error)
{	
	if (error == QProcess::FailedToStart || error == QProcess::Timedout)
	{
		if (!!(heimdallState & HeimdallState::Flashing))
		{
			flashLabel->setText("Failed to start Heimdall!");
			flashProgressBar->setEnabled(false);
		}
		else
		{
			utilityOutputPlainTextEdit->setPlainText("\nFRONTEND ERROR: Failed to start Heimdall!\n" + QString::fromUtf8(heimdallProcess.readAllStandardError()));
		}

		heimdallFailed = true;
		heimdallState = HeimdallState::Stopped;
		UpdateInterfaceAvailability();
	}
	else if (error == QProcess::Crashed)
	{
		if (!!(heimdallState & HeimdallState::Flashing))
		{
			flashLabel->setText("Heimdall crashed!");
			flashProgressBar->setEnabled(false);
		}
		else
		{
			utilityOutputPlainTextEdit->appendPlainText("\nFRONTEND ERROR: Heimdall crashed!\n" + QString::fromUtf8(heimdallProcess.readAllStandardError()));
		}
		
		heimdallState = HeimdallState::Stopped;
		UpdateInterfaceAvailability();
	}
	else
	{
		if (!!(heimdallState & HeimdallState::Flashing))
		{
			flashLabel->setText("Heimdall reported an unknown error!");
			flashProgressBar->setEnabled(false);
		}
		else
		{
			utilityOutputPlainTextEdit->appendPlainText("\nFRONTEND ERROR: Heimdall reported an unknown error!\n" + QString::fromUtf8(heimdallProcess.readAllStandardError()));
		}
		
		heimdallState = HeimdallState::Stopped;
		UpdateInterfaceAvailability();
	}
}

// ADB Commands Implementation

void MainWindow::RebootToRecovery(void)
{
	adbStatusLabel->setText("ADB Status: Rebooting to recovery...");
	adbOutputTextEdit->append("Executing: " + HeimdallFrontend::Adb::adbExecutable() + " reboot recovery");
    
	QStringList arguments = HeimdallFrontend::Adb::argsRebootRecovery();
    
	adbProcess.start(HeimdallFrontend::Adb::adbExecutable(), arguments);
}

void MainWindow::RebootToDownload(void)
{
	adbStatusLabel->setText("ADB Status: Rebooting to download mode...");
	adbOutputTextEdit->append("Executing: " + HeimdallFrontend::Adb::adbExecutable() + " reboot download");
    
	QStringList arguments = HeimdallFrontend::Adb::argsRebootDownload();
    
	adbProcess.start(HeimdallFrontend::Adb::adbExecutable(), arguments);
}

void MainWindow::RebootToFastboot(void)
{
	adbStatusLabel->setText("ADB Status: Rebooting to fastboot...");
	adbOutputTextEdit->append("Executing: " + HeimdallFrontend::Adb::adbExecutable() + " reboot bootloader");
    
	QStringList arguments = HeimdallFrontend::Adb::argsRebootFastboot();
    
	adbProcess.start(HeimdallFrontend::Adb::adbExecutable(), arguments);
}

void MainWindow::ShutdownDevice(void)
{
	adbStatusLabel->setText("ADB Status: Shutting down device...");
	adbOutputTextEdit->append("Executing: " + HeimdallFrontend::Adb::adbExecutable() + " shell reboot -p");
    
	QStringList arguments = HeimdallFrontend::Adb::argsShutdown();
    
	adbProcess.start(HeimdallFrontend::Adb::adbExecutable(), arguments);
}

void MainWindow::ExecuteCustomAdbCommand(void)
{
	QString command = customAdbCommandLineEdit->text().trimmed();
	if (command.isEmpty())
		return;
		
	adbStatusLabel->setText("ADB Status: Executing custom command...");
	adbOutputTextEdit->append("Executing: " + HeimdallFrontend::Adb::adbExecutable() + " " + command);
    
	QStringList arguments = HeimdallFrontend::Adb::argsCustom(command);
    
	adbProcess.start(HeimdallFrontend::Adb::adbExecutable(), arguments);
}

void MainWindow::RefreshDeviceInfo(void)
{
	adbStatusLabel->setText("ADB Status: Getting device information...");
	deviceInfoTextEdit->append("=== Device Information ===");
    
	QStringList arguments = HeimdallFrontend::Adb::argsCustom("shell getprop");
    
	adbProcess.start(HeimdallFrontend::Adb::adbExecutable(), arguments);
}

void MainWindow::UpdateAdbInterface(void)
{
	UpdateInterfaceAvailability();
}

void MainWindow::ListAdbDevices(void)
{
	adbStatusLabel->setText("ADB Status: Listing connected devices...");
	adbOutputTextEdit->append("<br><font color='#4A90E2'>=== 📱 ADB DEVICES ===</font>");
	adbOutputTextEdit->append("<font color='#4A90E2'>Executing: " + HeimdallFrontend::Adb::adbExecutable() + " devices -l</font>");
    
	QStringList arguments = HeimdallFrontend::Adb::argsDevices();
    
	adbProcess.start(HeimdallFrontend::Adb::adbExecutable(), arguments);
}

void MainWindow::AdbShellLs(void)
{
	adbStatusLabel->setText("ADB Status: Listing root directory...");
	adbOutputTextEdit->append("<br><font color='#4A90E2'>=== 📁 SHELL LS -LA / ===</font>");
	adbOutputTextEdit->append("<font color='#4A90E2'>Executing: " + HeimdallFrontend::Adb::adbExecutable() + " shell ls -la /</font>");
    
	QStringList arguments = HeimdallFrontend::Adb::argsShellLsRoot();
    
	adbProcess.start(HeimdallFrontend::Adb::adbExecutable(), arguments);
}

void MainWindow::AdbLogcat(void)
{
	adbStatusLabel->setText("ADB Status: Getting recent logs...");
	adbOutputTextEdit->append("<br><font color='#4A90E2'>=== 📝 RECENT LOGCAT ===</font>");
	adbOutputTextEdit->append("<font color='#4A90E2'>Executing: " + HeimdallFrontend::Adb::adbExecutable() + " logcat -d -t 50</font>");
    
	QStringList arguments = HeimdallFrontend::Adb::argsLogcatRecent(50);
    
	adbProcess.start(HeimdallFrontend::Adb::adbExecutable(), arguments);
}

void MainWindow::CheckRoot(void)
{
	adbStatusLabel->setText("ADB Status: Checking root access...");
	adbOutputTextEdit->append("<br><font color='#4A90E2'>=== 🔐 CHECKING ROOT ACCESS ===</font>");
	adbOutputTextEdit->append("<font color='#4A90E2'>Executing: " + HeimdallFrontend::Adb::adbExecutable() + " shell which su</font>");
    
	QStringList arguments = HeimdallFrontend::Adb::argsCheckRoot();
    
	adbProcess.start(HeimdallFrontend::Adb::adbExecutable(), arguments);
}

void MainWindow::InstallApk(void)
{
	QString apkPath = PromptFileSelection("Select APK file to install", "Android Package (*.apk)");
	
	if (apkPath.isEmpty())
		return;
		
	adbStatusLabel->setText("ADB Status: Installing APK...");
	adbOutputTextEdit->append("<br><font color='#4A90E2'>=== 📦 INSTALLING APK ===</font>");
	adbOutputTextEdit->append("<font color='#4A90E2'>Executing: " + HeimdallFrontend::Adb::adbExecutable() + " install " + apkPath + "</font>");
    
	QStringList arguments = HeimdallFrontend::Adb::argsInstallApk(apkPath);
    
	adbProcess.start(HeimdallFrontend::Adb::adbExecutable(), arguments);
}

void MainWindow::ClearAdbOutput(void)
{
	adbOutputTextEdit->clear();
	adbOutputTextEdit->append("<font color='#4A90E2'>ADB output cleared - ready for new commands ✨</font>");
	adbStatusLabel->setText("ADB Status: Output cleared");
}

void MainWindow::HandleAdbStdout(void)
{
	QByteArray data = adbProcess.readAll();
	QString output = QString::fromUtf8(data);
	
	// Remove the command prefix if it shows up in output
	if (output.startsWith("Executing: "))
		return;
	
	if (adbProcess.arguments().contains("getprop"))
	{
		deviceInfoTextEdit->append(output);
	}
	else
	{
		// Add timestamp and color for better readability
		if (!output.trimmed().isEmpty())
		{
			QString coloredOutput = output.trimmed();
			
			// Check for root detection specifically
			if (adbProcess.arguments().contains("which") && adbProcess.arguments().contains("su"))
			{
				if (coloredOutput.contains("/system/bin/su") || coloredOutput.contains("/su"))
				{
					coloredOutput = "<font color='#4CAF50'>✅ ROOT ACCESS DETECTED: " + coloredOutput + "</font>";
				}
				else if (coloredOutput.isEmpty())
				{
					coloredOutput = "<font color='#FF5722'>❌ NO ROOT ACCESS - 'su' command not found</font>";
				}
			}
			// Color success messages
			else if (coloredOutput.contains("Success") || coloredOutput.contains("successfully"))
			{
				coloredOutput = "<font color='#4CAF50'>" + coloredOutput + "</font>";
			}
			// Color error messages
			else if (coloredOutput.contains("error") || coloredOutput.contains("failed") || coloredOutput.contains("Error"))
			{
				coloredOutput = "<font color='#FF5722'>" + coloredOutput + "</font>";
			}
			// Color device information
			else if (coloredOutput.contains("device") && coloredOutput.contains("\t"))
			{
				coloredOutput = "<font color='#4CAF50'>" + coloredOutput + "</font>";
			}
			// Default output in light gray
			else
			{
				coloredOutput = "<font color='#B0BEC5'>" + coloredOutput + "</font>";
			}
			
			adbOutputTextEdit->append(coloredOutput);
		}
	}
}

void MainWindow::HandleAdbReturned(int exitCode, QProcess::ExitStatus exitStatus)
{
	if (exitStatus == QProcess::NormalExit)
	{
		if (exitCode == 0)
		{
			adbStatusLabel->setText("ADB Status: Command completed successfully");
			
			// Add helpful completion messages
			if (adbProcess.arguments().contains("devices"))
			{
				adbOutputTextEdit->append("\n--- Device list complete ---\n");
			}
			else if (adbProcess.arguments().contains("install"))
			{
				adbOutputTextEdit->append("\n--- APK installation complete ---\n");
			}
			else if (adbProcess.arguments().contains("logcat"))
			{
				adbOutputTextEdit->append("\n--- Logcat dump complete ---\n");
			}
		}
		else
		{
			adbStatusLabel->setText("ADB Status: Command failed (exit code: " + QString::number(exitCode) + ")");
			
			// Provide helpful error suggestions
			if (exitCode == 1)
			{
				if (adbProcess.arguments().contains("shell"))
				{
					adbOutputTextEdit->append("\nHINT: Shell command failed. Check device connection or try a different path/command.");
				}
				else
				{
					adbOutputTextEdit->append("\nHINT: Command failed. Make sure device is connected and ADB is authorized.");
				}
			}
			adbOutputTextEdit->append("Command failed with exit code: " + QString::number(exitCode));
		}
	}
	else
	{
		adbStatusLabel->setText("ADB Status: Command crashed");
		adbOutputTextEdit->append("ERROR: Command crashed!");
	}
	
	UpdateInterfaceAvailability();
}

void MainWindow::HandleAdbError(QProcess::ProcessError error)
{
	QString errorString;
	switch (error)
	{
		case QProcess::FailedToStart:
			errorString = "Failed to start ADB. Is ADB installed and in PATH?";
			adbOutputTextEdit->append("\nTROUBLESHOOTING:");
			adbOutputTextEdit->append("1. Install Android SDK Platform Tools");
			adbOutputTextEdit->append("2. Add ADB to system PATH");
			adbOutputTextEdit->append("3. Enable USB Debugging on device");
			break;
		case QProcess::Crashed:
			errorString = "ADB process crashed";
			break;
		case QProcess::Timedout:
			errorString = "ADB process timed out";
			break;
		case QProcess::ReadError:
			errorString = "ADB read error";
			break;
		case QProcess::WriteError:
			errorString = "ADB write error";
			break;
		default:
			errorString = "Unknown ADB error";
			break;
	}
	
	adbStatusLabel->setText("ADB Status: Error - " + errorString);
	adbOutputTextEdit->append("ERROR: " + errorString);
	
	UpdateInterfaceAvailability();
}

// Theme System Implementation

void MainWindow::FollowSystemTheme(void)
{
	// Uncheck other theme actions
	actionLightTheme->setChecked(false);
	actionDarkTheme->setChecked(false);
	actionFollowSystem->setChecked(true);
	
	currentTheme = 0;
	DetectSystemTheme();
}

void MainWindow::LightTheme(void)
{
	// Uncheck other theme actions
	actionFollowSystem->setChecked(false);
	actionDarkTheme->setChecked(false);
	actionLightTheme->setChecked(true);
	
	currentTheme = 1;
	ApplyTheme(1);
}

void MainWindow::DarkTheme(void)
{
	// Uncheck other theme actions
	actionFollowSystem->setChecked(false);
	actionLightTheme->setChecked(false);
	actionDarkTheme->setChecked(true);
	
	currentTheme = 2;
	ApplyTheme(2);
}

void MainWindow::DetectSystemTheme(void)
{
	// Try to detect system theme (simplified detection)
	QPalette palette = QApplication::palette();
	QColor backgroundColor = palette.color(QPalette::Window);
	
	// If background is dark, assume dark theme
	bool isDarkTheme = backgroundColor.lightness() < 128;
	ApplyTheme(isDarkTheme ? 2 : 1);
}

void MainWindow::ApplyTheme(int themeType)
{
	QString styleSheet;
	
	if (themeType == 0) // Follow System
	{
		DetectSystemTheme();
		return;
	}
	else if (themeType == 1) // Light Theme
	{
		styleSheet = R"(
/* Light Theme */
QMainWindow {
    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                stop: 0 #F8F9FA, stop: 1 #E9ECEF);
    color: #212529;
}

QGroupBox {
    font-weight: bold;
    border: 2px solid #DEE2E6;
    border-radius: 8px;
    margin-top: 10px;
    padding: 8px;
    background: white;
}

QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 8px 0 8px;
    color: #495057;
    background: white;
}

QPushButton {
    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                stop: 0 #4A90E2, stop: 1 #357ABD);
    border: 1px solid #2E5984;
    border-radius: 6px;
    color: white;
    font-weight: bold;
    padding: 6px 12px;
    min-width: 80px;
}

QPushButton:hover {
    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                stop: 0 #5BA0F2, stop: 1 #4682CD);
}

QPushButton:pressed {
    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                stop: 0 #3A7BC2, stop: 1 #286AAD);
}

QPushButton:disabled {
    background: #ADB5BD;
    border-color: #6C757D;
    color: #6C757D;
}

QLineEdit, QTextEdit, QPlainTextEdit {
    background: white;
    border: 2px solid #CED4DA;
    border-radius: 6px;
    padding: 6px;
    color: #212529;
    selection-background-color: #4A90E2;
    selection-color: white;
}

QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus {
    border-color: #4A90E2;
    background: #F8F9FA;
}

QComboBox {
    background: white;
    border: 2px solid #CED4DA;
    border-radius: 6px;
    padding: 4px 8px;
    color: #212529;
    min-width: 6em;
}

QComboBox:focus {
    border-color: #4A90E2;
}

QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 20px;
    border-left: 1px solid #CED4DA;
}

QListWidget {
    background: white;
    border: 2px solid #CED4DA;
    border-radius: 6px;
    color: #212529;
    alternate-background-color: #F8F9FA;
}

QListWidget::item:selected {
    background: #4A90E2;
    color: white;
}

QListWidget::item:hover {
    background: #E3F2FD;
}

QProgressBar {
    border: 2px solid #CED4DA;
    border-radius: 6px;
    background: #E9ECEF;
    text-align: center;
}

QProgressBar::chunk {
    background: qlineargradient(x1: 0, y1: 0, x2: 1, y2: 0,
                                stop: 0 #28A745, stop: 1 #20C997);
    border-radius: 4px;
}

QLabel {
    color: #495057;
}
)";
	}
	else if (themeType == 2) // Dark Theme
	{
		styleSheet = R"(
/* Dark Theme */
QMainWindow {
    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                stop: 0 #2B2B2B, stop: 1 #1E1E1E);
    color: #E0E0E0;
}

QGroupBox {
    font-weight: bold;
    border: 2px solid #404040;
    border-radius: 8px;
    margin-top: 10px;
    padding: 8px;
    background: #383838;
}

QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 8px 0 8px;
    color: #E0E0E0;
    background: #383838;
}

QPushButton {
    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                stop: 0 #4A90E2, stop: 1 #357ABD);
    border: 1px solid #2E5984;
    border-radius: 6px;
    color: white;
    font-weight: bold;
    padding: 6px 12px;
    min-width: 80px;
}

QPushButton:hover {
    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                stop: 0 #5BA0F2, stop: 1 #4682CD);
}

QPushButton:pressed {
    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                stop: 0 #3A7BC2, stop: 1 #286AAD);
}

QPushButton:disabled {
    background: #555555;
    border-color: #777777;
    color: #AAAAAA;
}

QLineEdit, QTextEdit, QPlainTextEdit {
    background: #2B2B2B;
    border: 2px solid #555555;
    border-radius: 6px;
    padding: 6px;
    color: #E0E0E0;
    selection-background-color: #4A90E2;
    selection-color: white;
}

QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus {
    border-color: #4A90E2;
    background: #333333;
}

QComboBox {
    background: #2B2B2B;
    border: 2px solid #555555;
    border-radius: 6px;
    padding: 4px 8px;
    color: #E0E0E0;
    min-width: 6em;
}

QComboBox:focus {
    border-color: #4A90E2;
}

QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 20px;
    border-left: 1px solid #555555;
}

QListWidget {
    background: #2B2B2B;
    border: 2px solid #555555;
    border-radius: 6px;
    color: #E0E0E0;
    alternate-background-color: #333333;
}

QListWidget::item:selected {
    background: #4A90E2;
    color: white;
}

QListWidget::item:hover {
    background: #404040;
}

QProgressBar {
    border: 2px solid #555555;
    border-radius: 6px;
    background: #1E1E1E;
    text-align: center;
    color: #E0E0E0;
}

QProgressBar::chunk {
    background: qlineargradient(x1: 0, y1: 0, x2: 1, y2: 0,
                                stop: 0 #28A745, stop: 1 #20C997);
    border-radius: 4px;
}

QLabel {
    color: #E0E0E0;
}
)";
	}
	
	// Apply theme to main interface
	functionTabWidget->setStyleSheet(styleSheet);
	
	this->setStyleSheet(styleSheet);
}

// Responsive handling
bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
	if (event->type() == QEvent::Resize)
	{
		// Force update of non-layout widgets when window resizes
		QResizeEvent *resizeEvent = static_cast<QResizeEvent*>(event);
		adaptWidgetsToSize(resizeEvent->size());
	}
	return QMainWindow::eventFilter(obj, event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
	QMainWindow::resizeEvent(event);
	adaptWidgetsToSize(event->size());
}

void MainWindow::adaptWidgetsToSize(const QSize &size)
{
	// Get available size for tabs (minus header and margins)
	int availableWidth = size.width() - 20;  // Margins
	int availableHeight = size.height() - 100; // Header + margins + menu
	
	// Force tabs to use available space for non-layout tabs
	if (functionTabWidget->currentWidget())
	{
		QWidget *currentTab = functionTabWidget->currentWidget();
		
		// Flash Tab responsive adjustments
		if (currentTab->objectName() == "flashTab")
		{
			if (auto statusGroup = currentTab->findChild<QGroupBox*>("statusGroup"))
			{
				statusGroup->resize(availableWidth * 0.6, 170);
				statusGroup->move(10, availableHeight - 180);
			}
		}
		// Load Package Tab is fully layout-managed; avoid manual move/resize
		else if (currentTab->objectName() == "loadPackageTab")
		{
			// No manual geometry changes here; layouts handle responsiveness
		}
		// Create Package Tab responsive adjustments
		else if (currentTab->objectName() == "createPackageTab")  
		{
			// Similar adjustments for create package tab
		}
		// Utilities Tab responsive adjustments
		else if (currentTab->objectName().startsWith("tab"))
		{
			// Utilities tab adjustments
		}
	}
}
