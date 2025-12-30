#include "TEEAnalyzer.h"

#include <QRegExp>

namespace HeimdallFrontend {
namespace TEE {

static void addIfContains(QStringList& indicators, const QString& haystack, const QString& needle)
{
    if (haystack.contains(needle, Qt::CaseInsensitive))
        indicators << needle;
}

static void addIfAny(QStringList& indicators, const QString& haystack, const QStringList& needles)
{
    for (const auto& n : needles)
        addIfContains(indicators, haystack, n);
}

TeeAnalysisResult analyze(const QString& props,
                          const QString& devNodes,
                          const QString& kernelLog,
                          const QStringList& vendorLibs)
{
    TeeAnalysisResult res;
    res.type = TeeType::Unknown;
    res.typeName = "Unknown";
    res.confidence = 0;

    QStringList hints;

    // Qualcomm QSEE (QTEE)
    int qseeScore = 0;
    addIfAny(hints, props, {"qcom", "qsee", "qtee", "keymaster.qcom"});
    addIfContains(hints, devNodes, "qseecom");
    addIfAny(hints, kernelLog, {"qseecom", "qsee", "qtee"});
    for (const auto& lib : vendorLibs) {
        if (lib.contains("QSEE", Qt::CaseInsensitive) || lib.contains("QTEE", Qt::CaseInsensitive) || lib.contains("QSEECom", Qt::CaseInsensitive))
            hints << lib;
    }
    qseeScore = hints.filter(QRegExp("qsee|qtee|qcom|QSEE|QTEE", Qt::CaseInsensitive)).size();

    // OP-TEE
    int opteeScore = 0;
    addIfAny(hints, devNodes, {"/dev/tee0", "/dev/teepriv0"});
    addIfAny(hints, kernelLog, {"optee", "tee core"});
    addIfAny(hints, props, {"optee", "keymaster.optee"});
    opteeScore += hints.filter(QRegExp("optee|teepriv|/dev/tee", Qt::CaseInsensitive)).size();

    // Trustonic Kinibi / Mobicore
    int trustonicScore = 0;
    addIfAny(hints, props, {"trustonic", "mobicore", "kinibi"});
    addIfAny(hints, kernelLog, {"trustonic", "mobicore", "kinibi"});
    for (const auto& lib : vendorLibs) {
        if (lib.contains("McClient", Qt::CaseInsensitive) || lib.contains("trustonic", Qt::CaseInsensitive) || lib.contains("mobicore", Qt::CaseInsensitive))
            hints << lib;
    }
    trustonicScore += hints.filter(QRegExp("trustonic|mobicore|kinibi|McClient", Qt::CaseInsensitive)).size();

    // Samsung TEEgris / TIMA
    int teegrisScore = 0;
    addIfAny(hints, props, {"teegris", "tima"});
    addIfAny(hints, kernelLog, {"teegris", "tima"});
    for (const auto& lib : vendorLibs) {
        if (lib.contains("teegris", Qt::CaseInsensitive) || lib.contains("tima", Qt::CaseInsensitive))
            hints << lib;
    }
    teegrisScore += hints.filter(QRegExp("teegris|tima", Qt::CaseInsensitive)).size();

    // MediaTek Microtrust
    int mtkScore = 0;
    addIfAny(hints, props, {"mtk", "microtrust"});
    addIfAny(hints, kernelLog, {"microtrust", "mtk tee"});
    mtkScore += hints.filter(QRegExp("microtrust|mtk", Qt::CaseInsensitive)).size();

    // Huawei iTEE
    int hisiScore = 0;
    addIfAny(hints, props, {"hisi", "huawei", "itee"});
    addIfAny(hints, kernelLog, {"hisi", "itee"});
    hisiScore += hints.filter(QRegExp("itee|hisi|huawei", Qt::CaseInsensitive)).size();

    // Google StrongBox / Titan M (not a general TEE, but keymaster StrongBox)
    int strongboxScore = 0;
    addIfAny(hints, props, {"strongbox", "titan_m"});
    addIfAny(hints, kernelLog, {"strongbox"});
    strongboxScore += hints.filter(QRegExp("strongbox|titan", Qt::CaseInsensitive)).size();

    // Choose classification by highest score
    struct Candidate { TeeType type; const char* name; int score; };
    QList<Candidate> cands = {
        {TeeType::QualcommQSEE, "Qualcomm QSEE (QTEE)", qseeScore},
        {TeeType::OPTEE, "OP-TEE", opteeScore},
        {TeeType::TrustonicKinibi, "Trustonic Kinibi (Mobicore)", trustonicScore},
        {TeeType::SamsungTEEgris, "Samsung TEEgris (TIMA)", teegrisScore},
        {TeeType::MediaTekMicrotrust, "MediaTek Microtrust", mtkScore},
        {TeeType::HuaweiiTEE, "Huawei iTEE", hisiScore},
        {TeeType::GoogleStrongBoxTitanM, "Google StrongBox (Titan M)", strongboxScore}
    };

    Candidate best {TeeType::Unknown, "Unknown", 0};
    for (const auto& c : cands) {
        if (c.score > best.score) best = c;
    }

    res.type = best.type;
    res.typeName = QString::fromLatin1(best.name);
    res.confidence = qBound(0, best.score * 12, 100); // simple scaling
    res.indicators = hints;
    return res;
}

} // namespace TEE
} // namespace HeimdallFrontend
