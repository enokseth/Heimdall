/*
 * ADB command argument helpers for Heimdall Frontend.
 * Separates ADB command construction from UI code.
 */

#ifndef HEIMDALL_FRONTEND_ADBCOMMANDS_H
#define HEIMDALL_FRONTEND_ADBCOMMANDS_H

#include <QString>
#include <QStringList>

namespace HeimdallFrontend {
namespace Adb {

// Returns the adb executable name (future-proof for customization)
QString adbExecutable();

// Common ADB command argument builders
QStringList argsRebootRecovery();
QStringList argsRebootDownload();
QStringList argsRebootFastboot();
QStringList argsShutdown();

QStringList argsCustom(const QString& commandLine);

QStringList argsDevices();
QStringList argsShellLsRoot();
QStringList argsLogcatRecent(int lines = 50);
QStringList argsCheckRoot();
QStringList argsInstallApk(const QString& apkPath);

} // namespace Adb
} // namespace HeimdallFrontend

#endif // HEIMDALL_FRONTEND_ADBCOMMANDS_H
