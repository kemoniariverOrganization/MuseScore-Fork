#ifndef ODLADRIVER_H
#define ODLADRIVER_H

#include <QObject>
#include <QLocalSocket>
#include "scoreview.h"
#include "libmscore/spanner.h"
#include "libmscore/select.h"

//namespace Ms {
//    class ScoreView;
//}

namespace ODLA {

/*!
 * \brief The ODLADriver class
 */
class ODLADriver : public QObject
{
    Q_OBJECT
public:
    explicit ODLADriver(QObject *parent = nullptr);

    void setScoreView(Ms::ScoreView* scoreView);

    static QString nextValidFileName(QString prefix, QString ext = "mscx");
    static QString nextUntitledSuffixNumber(QString untitledNamePrefix, QString ext = "mscx");

signals:

public slots:
    void init();
    void setCurrentScore(Ms::MasterScore* current);

protected slots:
    void onConnected();
    void onIncomingData();

protected:
    QLocalSocket* _localSocket;
    Ms::MasterScore* _currentScore;
    Ms::ScoreView* _scoreView;
    QString _untitledPrefix;

    bool _editingChord;

    void setNoteEntryMode(bool enabled);
    bool addSpannerToCurrentSelection(Ms::Spanner* spanner);
};

} // namespace ODLA

#endif // ODLADRIVER_H
