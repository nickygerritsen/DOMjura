#include "resultswindow.h"

#include <QKeyEvent>
#include <QApplication>
#include <QDesktopWidget>
#include <QPropertyAnimation>
#include <QDebug>
#include <QtOpenGL/QtOpenGL>
#include <QTimer>
#include <math.h>

#include "defines.h"

namespace DJ {
namespace View {

ResultsWindow::ResultsWindow(QWidget *parent) : QGraphicsView(parent) {
	this->scrollToBottomAnim = NULL;
	this->legendaAnim = NULL;
	this->windowClosed = true;

	this->started = false;
	this->offset = 0.0;
	this->canDoNextStep = true;
	this->lastResolvTeam = -1;
	this->currentResolvIndex = -1;

	this->scene = new QGraphicsScene(this);
	this->scene->setSceneRect(QApplication::desktop()->screenGeometry());
	this->scene->setBackgroundBrush(Qt::black);

	this->setScene(this->scene);
	this->setGeometry(QApplication::desktop()->screenGeometry());

	this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	this->centerOn(0, 0);

	this->setViewportUpdateMode(FullViewportUpdate);
	this->setCacheMode(CacheBackground);
	this->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
	this->setViewport(new QGLWidget(QGLFormat(QGL::SampleBuffers)));

	this->headerItem = new HeaderGraphicsItem(QApplication::desktop()->screenGeometry().width());
	this->headerItem->setPos(0, 0);

	this->legendaItem = new LegendaGraphicsItem();
	QRectF legendaRect = this->legendaItem->boundingRect();
	this->legendaItem->setPos(QApplication::desktop()->screenGeometry().width() - legendaRect.width() - LEGENDA_RIGHT_OFFSET,
							  QApplication::desktop()->screenGeometry().height() - legendaRect.height() - LEGENDA_BOTTOM_OFFSET);
	this->legendaItem->setZValue(1);

	this->pixmap = new QGraphicsPixmapItem;
	this->pixmap->setZValue(1);

	this->scene->addItem(this->pixmap);
	this->scene->addItem(this->headerItem);
	this->scene->addItem(this->legendaItem);

	this->legendaTimer = new QTimer(this);
	this->legendaTimer->setSingleShot(true);
	connect(this->legendaTimer, SIGNAL(timeout()), this, SLOT(hideLegenda()));
}

void ResultsWindow::setBrandingImageFile(QString filename) {
	if (!filename.isEmpty()) {
		QPixmap pixmap(filename);
		this->pixmap->setPixmap(pixmap);
	} else {
		this->pixmap->setPixmap(QPixmap());
	}
	resizeImage();
}

void ResultsWindow::showFullScreen() {
	this->windowClosed = false;
	QGraphicsView::showFullScreen();
}

void ResultsWindow::setTeams(QList<ResultTeam> teams, bool animated, int lastResolvedTeam, int lastResolvedProblem, int currentTeam) {
	if (animated) {
		qDebug() << "--------------DEBUG--------------";
		qDebug() << "last resolved team:   " << lastResolvedTeam;
		qDebug() << "last resolved problem:" << lastResolvedProblem;
		qDebug() << "current team:         " << currentTeam;

		if (this->lastResolvTeam >= 0) {
			this->teamItems.at(this->lastResolvTeam)->setHighlighted(false);
		}

		// Save all data
		this->teamsToSet = teams;
		this->lastResolvTeam = lastResolvedTeam;
		this->lastResolvProblem = lastResolvedProblem;
		this->currentResolvIndex = currentTeam;
		this->teamItems.at(this->lastResolvTeam)->setHighlighted(true);

		int screenHeight = QApplication::desktop()->screenGeometry().height();
		int itemToScrollHeight = HEADER_HEIGHT + (this->lastResolvTeam + 1) * TEAMITEM_HEIGHT + RESOLV_BELOW_OFFSET;
		int toScroll = qMax(0, itemToScrollHeight - screenHeight);
		QPointF toScrollPoint(0, toScroll);
		if (this->headerItem->pos().y() == -toScroll) {
			// start a timer that waits a second
			QTimer *timer = new QTimer;
			timer->setSingleShot(true);
			connect(timer, SIGNAL(timeout()), this, SLOT(timerDone()));
			timer->start(1000);
		} else {

			// First, move to the the row to highlight
			QParallelAnimationGroup *scrollToRowAnim = new QParallelAnimationGroup;
			scrollToRowAnim->setProperty("DJ_animType", "toRow");
			connect(scrollToRowAnim, SIGNAL(finished()), this, SLOT(animationDone()));

			QPropertyAnimation *animHeader = new QPropertyAnimation(this->headerItem, "pos");
			animHeader->setDuration(1000);
			animHeader->setStartValue(this->headerItem->pos());
			animHeader->setEndValue(QPointF(0, 0) - toScrollPoint);
			scrollToRowAnim->addAnimation(animHeader);

			for (int i = 0; i < this->teamItems.size(); i++) {
				QPropertyAnimation *animItem = new QPropertyAnimation(this->teamItems.at(i), "pos");
				animItem->setDuration(1000);
				QPointF startPoint;
				startPoint.setX(0);
				startPoint.setY(HEADER_HEIGHT + i * TEAMITEM_HEIGHT);
				QPointF newPoint = startPoint;
				newPoint -= toScrollPoint;
				animItem->setStartValue(this->teamItems.at(i)->pos());
				animItem->setEndValue(newPoint);
				scrollToRowAnim->addAnimation(animItem);
			}

			scrollToRowAnim->start();
		}
	} else {
		for (int i = 0; i < this->teamItems.size(); i++) {
			this->scene->removeItem(this->teamItems.at(i));
			delete this->teamItems.at(i);
		}
		this->teamItems.clear();

		if (teams.size() == 0) {
			return;
		}
		this->teams = teams;
		int numprobs = teams.at(0).problems.size();
		int probWidth = (NAME_WIDTH - (numprobs - 1) * PROB_MARGIN) / numprobs;

		for (int i = 0; i < teams.size(); i++) {
			QList<ResultProblem> problems = teams.at(i).problems;
			if (problems.size() == 0) {
				return;
			}
			QList<ProblemGraphicsItem *> problemItems;
			for (int j = 0; j < numprobs; j++) {
				ProblemGraphicsItem *probItem = new ProblemGraphicsItem(0, probWidth);
				probItem->setProblemId(problems.at(j).problemId);
				probItem->setState(problems.at(j).state);
				probItem->setNumTries(problems.at(j).numTries);
				probItem->setTime(problems.at(j).time);
				problemItems.append(probItem);
			}

			TeamGraphicsItem *teamItem = new TeamGraphicsItem(problemItems);
			teamItem->setRank(teams.at(i).rank);
			teamItem->setName(teams.at(i).name);
			teamItem->setSolved(teams.at(i).solved);
			teamItem->setTime(teams.at(i).time);
			teamItem->setPos(0, HEADER_HEIGHT + i * TEAMITEM_HEIGHT + this->offset);
			teamItem->setEven(i % 2 == 0);

			this->teamItems.append(teamItem);
			this->scene->addItem(teamItem);
		}
	}
}

void ResultsWindow::keyPressEvent(QKeyEvent *event) {
	switch (event->key()) {
	case Qt::Key_Escape:
	case Qt::Key_Q:
	case Qt::Key_X:
		this->windowClosed = true;
		close();
		break;
	case Qt::Key_Enter:
	case Qt::Key_Return:
	case Qt::Key_Space:
		if (this->canDoNextStep) {
			doNextStep();
		}
	}
}

void ResultsWindow::mousePressEvent(QMouseEvent *event) {
	if (event->button() == Qt::LeftButton) {
		if (this->canDoNextStep) {
			doNextStep();
		}
	}
}

void ResultsWindow::reload() {
	this->offset = 0.0;
	this->started = false;
	this->canDoNextStep = true;
	this->currentResolvIndex = -1;
	this->lastResolvTeam = -1;

	if (this->scrollToBottomAnim) {
		this->scrollToBottomAnim->clear();
		delete this->scrollToBottomAnim;
		this->scrollToBottomAnim = NULL;
	}

	if (this->legendaAnim) {
		delete this->legendaAnim;
		this->legendaAnim = NULL;
	}
	this->headerItem->setPos(0, 0);
	this->hideLegendAfterTimeout();
}

void ResultsWindow::hideLegendAfterTimeout() {
	this->legendaItem->setOpacity(1);
	this->legendaTimer->start(5000);
}

void ResultsWindow::doNextStep() {
	if (!this->started) {
		if (this->scrollToBottomAnim) {
			this->scrollToBottomAnim->clear();
			delete this->scrollToBottomAnim;
			this->scrollToBottomAnim = NULL;
		}
		this->canDoNextStep = false;
		this->scrollToBottomAnim = new QParallelAnimationGroup;
		connect(this->scrollToBottomAnim, SIGNAL(finished()), this, SLOT(animationDone()));

		int screenHeight = QApplication::desktop()->screenGeometry().height();
		int totalItemsHeight = HEADER_HEIGHT + this->teamItems.size() * TEAMITEM_HEIGHT + SCROLL_BELOW_OFFSET;
		int toScroll = qMax(0, totalItemsHeight - screenHeight);
		QPointF toScrollPoint(0, toScroll);

		int timetoScroll = 1;
		if (toScroll > 0) {
			timetoScroll = 1000 + 1200 * log(this->teamItems.size());
		}

		QPropertyAnimation *animHeader = new QPropertyAnimation(this->headerItem, "pos");
		animHeader->setDuration(timetoScroll);
		animHeader->setEasingCurve(QEasingCurve::OutBack);
		animHeader->setStartValue(QPointF(0, 0));
		animHeader->setEndValue(QPointF(0, 0) - toScrollPoint);
		this->scrollToBottomAnim->addAnimation(animHeader);

		for (int i = 0; i < this->teamItems.size(); i++) {
			QPropertyAnimation *animItem = new QPropertyAnimation(this->teamItems.at(i), "pos");
			animItem->setDuration(timetoScroll);
			animItem->setEasingCurve(QEasingCurve::OutBack);
			QPointF startPoint;
			startPoint.setX(0);
			startPoint.setY(HEADER_HEIGHT + i * TEAMITEM_HEIGHT);
			QPointF newPoint = startPoint;
			newPoint -= toScrollPoint;
			animItem->setStartValue(startPoint);
			animItem->setEndValue(newPoint);
			this->scrollToBottomAnim->addAnimation(animItem);
		}

		this->scrollToBottomAnim->start();
	} else {
		this->canDoNextStep = false;
		emit newStandingNeeded();
	}
}

void ResultsWindow::animationDone() {
	if (this->sender() == this->scrollToBottomAnim) {
		if (this->windowClosed) {
			return;
		}
		this->started = true;
		this->canDoNextStep = true;
	} else if (this->sender()->property("DJ_animType") == "toRow") {
		this->sender()->deleteLater();
		if (this->windowClosed || this->lastResolvTeam < 0 || this->lastResolvTeam >= this->teamItems.size()) {
			return;
		}
		// start a timer that waits a second
		QTimer *timer = new QTimer;
		timer->setSingleShot(true);
		connect(timer, SIGNAL(timeout()), this, SLOT(timerDone()));
		timer->start(1000);
	} else if (this->sender()->property("DJ_animType") == "problemResolv") {
		this->sender()->deleteLater();

		if (this->windowClosed || this->lastResolvTeam < 0 || this->lastResolvTeam >= this->teamItems.size()) {
			return;
		}

		TeamGraphicsItem *team = this->teamItems.at(this->lastResolvTeam);
		ProblemGraphicsItem *problem = team->getProblemGraphicsItem(this->lastResolvProblem);

		problem->setHighlighted(false);

		if (problem->isSolved()) {
			// Determine where to move the row to
			int moveTo = this->lastResolvTeam;
			while (this->teamsToSet.at(moveTo).id != this->teams.at(this->lastResolvTeam).id) {
				moveTo--;
			}
			qDebug() << "This team will move to" << moveTo;
			// Now move the current team to moveTo and move all teams from moveTo until the current team one down
			QPointF moveToPoint = this->teamItems.at(moveTo)->pos();
			problem->setState(SOLVED);
		} else { // if the problem is not solved, just go to the next resolv
			this->offset = this->headerItem->pos().y();
			this->setTeams(this->teamsToSet);
			doNextStep();
		}
	}
}

void ResultsWindow::timerDone() {
	this->sender()->deleteLater();
	if (this->lastResolvTeam == this->currentResolvIndex) {
		if (this->windowClosed || this->lastResolvTeam < 0 || this->lastResolvTeam >= this->teamItems.size()) {
			return;
		}
		// Animate problem highlight
		TeamGraphicsItem *team = this->teamItems.at(this->lastResolvTeam);
		ProblemGraphicsItem *problem = team->getProblemGraphicsItem(this->lastResolvProblem);
		problem->setHighlighted(true);
		QPropertyAnimation *animHLC = new QPropertyAnimation(problem, "highlightColor");
		animHLC->setDuration(2000);
		animHLC->setKeyValueAt(0,     QColor(143, 124, 29));
		animHLC->setKeyValueAt(0.125, QColor(70, 62, 14));
		animHLC->setKeyValueAt(0.25,  QColor(143, 124, 29));
		animHLC->setKeyValueAt(0.375, QColor(70, 62, 14));
		animHLC->setKeyValueAt(0.5,   QColor(143, 124, 29));
		animHLC->setKeyValueAt(0.625, QColor(70, 62, 14));
		animHLC->setKeyValueAt(0.75,  QColor(143, 124, 29));
		animHLC->setKeyValueAt(0.875, QColor(70, 62, 14));
		if (problem->isSolved()) {
			animHLC->setKeyValueAt(1,     QColor(0, 128, 0));
		} else {
			animHLC->setKeyValueAt(1,     QColor(133, 0, 0));
		}

		QPropertyAnimation *animFC = new QPropertyAnimation(problem, "finalColor");
		animFC->setDuration(2000);
		animFC->setKeyValueAt(0,     QColor(255, 223, 54));
		animFC->setKeyValueAt(0.125, QColor(255, 223, 54));
		animFC->setKeyValueAt(0.25,  QColor(255, 223, 54));
		animFC->setKeyValueAt(0.375, QColor(255, 223, 54));
		animFC->setKeyValueAt(0.5,   QColor(255, 223, 54));
		animFC->setKeyValueAt(0.625, QColor(255, 223, 54));
		animFC->setKeyValueAt(0.75,  QColor(255, 223, 54));
		animFC->setKeyValueAt(0.875, QColor(255, 223, 54));
		if (problem->isSolved()) {
			animFC->setKeyValueAt(1,     QColor(0, 230, 0));
		} else {
			animFC->setKeyValueAt(1,     QColor(240, 0, 0));
		}
		QParallelAnimationGroup *parAnim = new QParallelAnimationGroup;
		parAnim->addAnimation(animHLC);
		parAnim->addAnimation(animFC);
		parAnim->setProperty("DJ_animType", "problemResolv");
		connect(parAnim, SIGNAL(finished()), this, SLOT(animationDone()));
		parAnim->start();
	} else {
		if (this->windowClosed || this->lastResolvTeam < 0 || this->lastResolvTeam >= this->teamItems.size()) {
			return;
		}
		this->offset = this->headerItem->pos().y();
		this->setTeams(this->teamsToSet);
		this->teamItems.at(this->lastResolvTeam)->setHighlighted(true);
		if (this->lastResolvTeam >= NEED_TO_CLICK) {
			doNextStep();
		} else {
			this->canDoNextStep = true;
		}
	}
}

void ResultsWindow::resizeImage() {
	QSize size;
	if (!this->pixmap->pixmap().isNull()) {
		size = this->pixmap->pixmap().size();
	} else {
		size = QSize(0, 0);
	}
	QRect screenSize = QApplication::desktop()->screenGeometry();
	QPointF labelPos;
	labelPos.setX(screenSize.width() - size.width());
	labelPos.setY(screenSize.height() - size.height());
	this->pixmap->setPos(labelPos);
}

void ResultsWindow::hideLegenda() {
	if (this->legendaAnim) {
		delete this->legendaAnim;
		this->legendaAnim = NULL;
	}
	this->legendaAnim = new QPropertyAnimation(this->legendaItem, "opacity");
	this->legendaAnim->setDuration(2000);
	this->legendaAnim->setEasingCurve(QEasingCurve::InOutExpo);
	this->legendaAnim->setStartValue(1);
	this->legendaAnim->setEndValue(0);

	this->legendaAnim->start();
}

int ResultsWindow::getCurrentResolvIndex() {
	return this->currentResolvIndex;
}

ResultTeam ResultsWindow::getResultTeam(int i) {
	return this->teams.at(i);
}

} // namespace View
} // namespace DJ
