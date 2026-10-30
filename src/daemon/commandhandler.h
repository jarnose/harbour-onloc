#pragma once

#include <QObject>
#include <QString>

class QProcess;
class QTimer;

// What the phone does when the server sends ring, lock or flash.
//
//  ring   plays the ringtone loudly for 30 seconds, whatever the profile, and
//         puts the media volume back afterwards. A press of the power button,
//         or unlocking the phone, ends it at once. paplay straight to
//         PulseAudio: going through the profile / ngfd would honour a silent
//         profile, and a silent phone is exactly the one being looked for.
//  lock   puts the device lock in its Locked state (so the code is needed at
//         once), then locks the touch screen and turns the display off
//         through mce. The message, if
//         any, is posted as a notification so it shows on the lock screen.
//  flash  blinks the display, as there is no torch LED on this device class
//         that a user session may drive.
//
// Everything here is best effort and logged: a command that cannot be carried
// out (no PulseAudio, no such D-Bus service) says so in the log and is dropped.
class CommandHandler : public QObject
{
    Q_OBJECT
public:
    explicit CommandHandler(QObject *parent = nullptr);

    void ring();
    void lock(const QString &message);
    void flash();

    // The last command and when, for the status file.
    QString lastCommand() const { return m_lastCommand; }

private slots:
    // mce signals; either one means someone is holding the phone.
    void onPowerButton(const QString &trigger);
    void onTklockMode(const QString &mode);

private:
    void startRingtone();
    void stopRing();
    void restoreVolume();
    static bool mce(const char *method, const QString &arg = QString());
    static bool notify(const QString &summary, const QString &body);
    static QString ringtonePath();

    QProcess *m_player = nullptr;
    QTimer *m_ringTimer;
    bool m_ringing = false;
    qint64 m_ringStartedMs = 0;
    int m_savedVolumePercent = -1;
    bool m_savedMuted = false;

    QTimer *m_flashTimer;
    int m_flashStepsLeft = 0;
    bool m_flashDisplayOn = false;
    bool m_flashRestoreOn = false;

    QString m_lastCommand;
};
