/*
 * ADB command argument helpers for Heimdall.
 * Placed in core source to separate concerns.
 */

#ifndef HEIMDALL_ADBCOMMANDS_H
#define HEIMDALL_ADBCOMMANDS_H

#include <QString>
#include <QStringList>

namespace HeimdallFrontend {
namespace Adb {

QString adbExecutable();

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
QStringList argsGetprop();

} // namespace Adb
} // namespace HeimdallFrontend

#endif // HEIMDALL_ADBCOMMANDS_H
