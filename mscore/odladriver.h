#ifndef ODLADRIVER_H
#define ODLADRIVER_H

#include <QObject>
#include <QtWebSockets/QtWebSockets>
#include <QtWebSockets/QWebSocketServer>
#include <QTreeWidget>
#include <QMap>
#include "scoreview.h"

enum selection_type_t: char
{
    NO_ELEMENT,
    SINGLE_ELEMENT,
    RANGE_ELEMENT,
};

/*!
 * \brief The ODLADriver class
 */
class ODLADriver : public QObject
{
    Q_OBJECT
public:
    enum SpeechField
    {
        NoteName        =   1<<0,
        DurationName    =   1<<1,
        BeatNumber      =   1<<2,
        MeasureNumber   =   1<<3,
        StaffNumber     =   1<<4,
        TimeSignFraction=   1<<5,
        ClefName        =   1<<6,
        KeySignName     =   1<<7,
        VoiceNumber     =   1<<8,
        BPMNumber       =   1<<9
    };
    Q_DECLARE_FLAGS(SpeechFields, SpeechField)
    Q_FLAG(SpeechFields)
    //friend QDataStream &operator>>(QDataStream &, QMap<QString> &);

    static ODLADriver* instance(QObject* parent = nullptr);
    void setScoreView(Ms::ScoreView* scoreView) {_scoreView = scoreView;};

private:
    ODLADriver(QObject *parent = nullptr);
    static ODLADriver * _instance;
    QWebSocket* _localSocket;
    QWebSocketServer* _localServer;
    Ms::MasterScore* _currentScore;
    Ms::ScoreView* _scoreView;
    QTreeWidget * _palette;
    bool _editingChord;
    bool _paused;

    QString getNoteName(Ms::Element *e);
    QString getDuration(Ms::Element *e);
    void getMeasureAndBeat(Ms::Element *e, QString &barString, QString &beatString);
    QString getClef(Ms::Element *e);
    QString getTimeSig(Ms::Element *e);
    QString getKeySignature(Ms::Element *e);
    QString getVoice(Ms::Element *e);
    QString getBPM(Ms::Element *e);
    QString getStaff(Ms::Element *e);
    Ms::Element *searchFromPalette(int paletteType, int cellIdx);
    void emulateDrop(Ms::Element *e, Ms::Element *target);
    void accBracket();    
    Ms::Element *currentElement();
    void tablatureReplacements(QString &command, int staffNum);
    void executeShortcut(QString command);

public slots:
    void onConnectionRequest();
    void setCurrentScore(Ms::MasterScore* current);

protected slots:
    void onConnected();
    void onIncomingData(const QString &odlaMessage);
    QMap<QString, QString> speechFeedback(ODLADriver::SpeechFields flags);
};

Q_DECLARE_OPERATORS_FOR_FLAGS(ODLADriver::SpeechFields)
#endif // ODLADRIVER_H
