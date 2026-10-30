#include "commandhandler.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusReply>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QProcess>
#include <QRegExp>
#include <QTimer>
#include <QVariantMap>

namespace {
constexpr int kRingMs = 30000;
constexpr int kFlashSteps = 12;
constexpr int kFlashStepMs = 450;

QByteArray pactl(const QStringList &args)
{
    QProcess p;
    p.start(QStringLiteral("pactl"), args);
    if (!p.waitForFinished(5000))
        return QByteArray();
    return p.readAllStandardOutput();
}
}

CommandHandler::CommandHandler(QObject *parent)
    : QObject(parent)
    , m_ringTimer(new QTimer(this))
    , m_flashTimer(new QTimer(this))
{
    m_ringTimer->setSingleShot(true);
    connect(m_ringTimer, &QTimer::timeout, this, &CommandHandler::stopRing);

    // Listen for the ways of saying "I found it". Connected once; the slots
    // ignore the signals unless a ring is going.
    QDBusConnection bus = QDBusConnection::systemBus();
    bus.connect(QStringLiteral("com.nokia.mce"), QStringLiteral("/com/nokia/mce/signal"),
                QStringLiteral("com.nokia.mce.signal"), QStringLiteral("power_button_trigger"),
                this, SLOT(onPowerButton(QString)));
    bus.connect(QStringLiteral("com.nokia.mce"), QStringLiteral("/com/nokia/mce/signal"),
                QStringLiteral("com.nokia.mce.signal"), QStringLiteral("tklock_mode_ind"),
                this, SLOT(onTklockMode(QString)));

    m_flashTimer->setInterval(kFlashStepMs);
    connect(m_flashTimer, &QTimer::timeout, this, [this]() {
        if (m_flashStepsLeft <= 0) {
            m_flashTimer->stop();
            // Leave the display as it was found.
            mce(m_flashRestoreOn ? "req_display_state_on" : "req_display_state_off");
            return;
        }
        --m_flashStepsLeft;
        m_flashDisplayOn = !m_flashDisplayOn;
        mce(m_flashDisplayOn ? "req_display_state_on" : "req_display_state_off");
    });
}

// ---- mce / notifications ------------------------------------------------------

bool CommandHandler::mce(const char *method, const QString &arg)
{
    QDBusMessage m = QDBusMessage::createMethodCall(
            QStringLiteral("com.nokia.mce"), QStringLiteral("/com/nokia/mce/request"),
            QStringLiteral("com.nokia.mce.request"), QString::fromLatin1(method));
    if (!arg.isEmpty())
        m << arg;
    const QDBusMessage reply = QDBusConnection::systemBus().call(m, QDBus::Block, 3000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning().noquote() << "mce" << method << "failed:" << reply.errorMessage();
        return false;
    }
    return true;
}

bool CommandHandler::notify(const QString &summary, const QString &body)
{
    QDBusMessage m = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
            QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    QVariantMap hints;
    hints.insert(QStringLiteral("urgency"), uchar(2));
    hints.insert(QStringLiteral("x-nemo-preview-summary"), summary);
    hints.insert(QStringLiteral("x-nemo-preview-body"), body);
    m << QStringLiteral("OnLoc") << uint(0) << QStringLiteral("icon-lock-information")
      << summary << body << QStringList() << hints << int(0);
    const QDBusMessage reply = QDBusConnection::sessionBus().call(m, QDBus::Block, 3000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning().noquote() << "notification failed:" << reply.errorMessage();
        return false;
    }
    return true;
}

// ---- ring ---------------------------------------------------------------------

QString CommandHandler::ringtonePath()
{
    const QDir dir(QStringLiteral("/usr/share/sounds/jolla-ringtones/stereo"));
    for (const QString &name : {QStringLiteral("jolla-ringtone.ogg")}) {
        if (dir.exists(name))
            return dir.filePath(name);
    }
    // Any ringtone at all beats none.
    const QStringList all = dir.entryList({QStringLiteral("*ringtone*.ogg")}, QDir::Files);
    return all.isEmpty() ? QString() : dir.filePath(all.first());
}

void CommandHandler::ring()
{
    m_lastCommand = QStringLiteral("ring @ ") + QDateTime::currentDateTime().toString(Qt::ISODate);
    qInfo() << "command: ring";
    // Another ring while ringing just extends it.
    m_ringTimer->start(kRingMs);
    if (m_ringing)
        return;

    const QString file = ringtonePath();
    if (file.isEmpty()) {
        qWarning() << "ring: no ringtone found";
        return;
    }

    // Remember the volume to put back, then go to full and unmute.
    const QByteArray vol = pactl({QStringLiteral("get-sink-volume"), QStringLiteral("@DEFAULT_SINK@")});
    QRegExp pct(QStringLiteral("(\\d+)%"));
    m_savedVolumePercent = pct.indexIn(QString::fromUtf8(vol)) >= 0 ? pct.cap(1).toInt() : -1;
    const QByteArray mute = pactl({QStringLiteral("get-sink-mute"), QStringLiteral("@DEFAULT_SINK@")});
    m_savedMuted = mute.contains("yes");
    pactl({QStringLiteral("set-sink-mute"), QStringLiteral("@DEFAULT_SINK@"), QStringLiteral("0")});
    pactl({QStringLiteral("set-sink-volume"), QStringLiteral("@DEFAULT_SINK@"), QStringLiteral("100%")});

    m_ringing = true;
    m_ringStartedMs = QDateTime::currentMSecsSinceEpoch();
    // Light the screen so the phone is easy to spot as well as to hear.
    mce("req_display_state_on");
    startRingtone();
}

void CommandHandler::onPowerButton(const QString &trigger)
{
    // A moment's grace: starting the ring lights the display, and a signal
    // from that must not end the ring it belongs to.
    if (!m_ringing || QDateTime::currentMSecsSinceEpoch() - m_ringStartedMs < 1500)
        return;
    qInfo().noquote() << "ring: stopped by the power button (" << trigger << ")";
    stopRing();
}

void CommandHandler::onTklockMode(const QString &mode)
{
    if (!m_ringing || mode != QLatin1String("unlocked")
            || QDateTime::currentMSecsSinceEpoch() - m_ringStartedMs < 1500)
        return;
    qInfo() << "ring: stopped, the phone was unlocked";
    stopRing();
}

void CommandHandler::startRingtone()
{
    if (!m_ringing)
        return;
    if (!m_player) {
        m_player = new QProcess(this);
        // The tone is a few seconds long; play it again until the timer ends the ring.
        connect(m_player, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                this, [this](int, QProcess::ExitStatus) { startRingtone(); });
    }
    m_player->start(QStringLiteral("paplay"), {ringtonePath()});
    if (!m_player->waitForStarted(2000)) {
        qWarning() << "ring: paplay did not start";
        stopRing();
    }
}

void CommandHandler::stopRing()
{
    if (!m_ringing)
        return;
    m_ringing = false;
    m_ringTimer->stop();
    if (m_player) {
        // finished() would restart the tone; m_ringing is already false, so it does not.
        m_player->kill();
        m_player->waitForFinished(1000);
    }
    restoreVolume();
    qInfo() << "ring: stopped";
}

void CommandHandler::restoreVolume()
{
    if (m_savedVolumePercent >= 0) {
        pactl({QStringLiteral("set-sink-volume"), QStringLiteral("@DEFAULT_SINK@"),
               QString::number(m_savedVolumePercent) + QLatin1Char('%')});
    }
    if (m_savedMuted)
        pactl({QStringLiteral("set-sink-mute"), QStringLiteral("@DEFAULT_SINK@"), QStringLiteral("1")});
    m_savedVolumePercent = -1;
    m_savedMuted = false;
}

// ---- lock / flash -------------------------------------------------------------

void CommandHandler::lock(const QString &message)
{
    m_lastCommand = QStringLiteral("lock @ ") + QDateTime::currentDateTime().toString(Qt::ISODate);
    qInfo() << "command: lock";
    // The device lock first: this is what makes the code necessary at once,
    // rather than after the phone's own "lock after" delay. The daemon accepts
    // only the Locked state (1) - anything else is refused with AccessDenied -
    // so this cannot lock anyone out, and it is refused when the device lock
    // is not in use, which is logged and leaves the screen lock below.
    {
        QDBusMessage m = QDBusMessage::createMethodCall(
                QStringLiteral("org.nemomobile.devicelock"), QStringLiteral("/devicelock"),
                QStringLiteral("org.nemomobile.lipstick.devicelock"), QStringLiteral("setState"));
        m << int(1);
        const QDBusMessage reply = QDBusConnection::systemBus().call(m, QDBus::Block, 3000);
        if (reply.type() == QDBusMessage::ErrorMessage)
            qWarning().noquote() << "device lock refused:" << reply.errorName() << reply.errorMessage();
    }
    mce("req_tklock_mode_change", QStringLiteral("locked"));
    mce("req_display_state_off");
    if (!message.trimmed().isEmpty())
        notify(tr("This phone was locked remotely"), message.trimmed());
}

void CommandHandler::flash()
{
    m_lastCommand = QStringLiteral("flash @ ") + QDateTime::currentDateTime().toString(Qt::ISODate);
    qInfo() << "command: flash";
    if (m_flashTimer->isActive())
        return;

    QDBusMessage m = QDBusMessage::createMethodCall(
            QStringLiteral("com.nokia.mce"), QStringLiteral("/com/nokia/mce/request"),
            QStringLiteral("com.nokia.mce.request"), QStringLiteral("get_display_status"));
    const QDBusMessage reply = QDBusConnection::systemBus().call(m, QDBus::Block, 3000);
    m_flashRestoreOn = !reply.arguments().isEmpty() && reply.arguments().first().toString() == QLatin1String("on");
    m_flashDisplayOn = m_flashRestoreOn;
    m_flashStepsLeft = kFlashSteps;
    m_flashTimer->start();
}
