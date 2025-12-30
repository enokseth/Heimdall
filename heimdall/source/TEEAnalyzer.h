#ifndef HEIMDALL_TEEANALYZER_H
#define HEIMDALL_TEEANALYZER_H

#include <QString>
#include <QStringList>

namespace HeimdallFrontend {
namespace TEE {

enum class TeeType {
    Unknown,
    QualcommQSEE,
    OPTEE,
    TrustonicKinibi,
    SamsungTEEgris,
    MediaTekMicrotrust,
    HuaweiiTEE,
    GoogleStrongBoxTitanM
};

struct TeeAnalysisResult {
    TeeType type;
    QString typeName;
    int confidence; // 0-100
    QStringList indicators; // matched hints
};

// Analyze multiple data sources to classify TEE implementation
TeeAnalysisResult analyze(const QString& props,
                          const QString& devNodes,
                          const QString& kernelLog,
                          const QStringList& vendorLibs);

} // namespace TEE
} // namespace HeimdallFrontend

#endif // HEIMDALL_TEEANALYZER_H
