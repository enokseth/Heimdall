#include "AdbCommands.h"

#include <QStringList>

namespace HeimdallFrontend {
namespace Adb {

QString adbExecutable()
{
    return QStringLiteral("adb");
}

QStringList argsRebootRecovery()
{
    QStringList args;
    args << "reboot" << "recovery";
    return args;
}

QStringList argsRebootDownload()
{
    QStringList args;
    args << "reboot" << "download";
    return args;
}

QStringList argsRebootFastboot()
{
    QStringList args;
    args << "reboot" << "bootloader";
    return args;
}

QStringList argsShutdown()
{
    QStringList args;
    args << "shell" << "reboot" << "-p";
    return args;
}

QStringList argsCustom(const QString& commandLine)
{
    return commandLine.split(' ', Qt::SkipEmptyParts);
}

QStringList argsDevices()
{
    QStringList args;
    args << "devices" << "-l";
    return args;
}

QStringList argsShellLsRoot()
{
    QStringList args;
    args << "shell" << "ls" << "-la" << "/";
    return args;
}

QStringList argsLogcatRecent(int lines)
{
    QStringList args;
    args << "logcat" << "-d" << "-t" << QString::number(lines);
    return args;
}

QStringList argsCheckRoot()
{
    QStringList args;
    args << "shell" << "which" << "su";
    return args;
}

QStringList argsInstallApk(const QString& apkPath)
{
    QStringList args;
    args << "install" << apkPath;
    return args;
}

QStringList argsGetprop()
{
    QStringList args;
    args << "shell" << "getprop";
    return args;
}

} // namespace Adb
} // namespace HeimdallFrontend
