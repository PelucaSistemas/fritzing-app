/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2007-2019 Fritzing

Fritzing is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Fritzing is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Fritzing.  If not, see <http://www.gnu.org/licenses/>.

********************************************************************/

#include "commands.h"
#include "sketch/sketchwidget.h"
#include "waitpushundostack.h"
#include "items/wire.h"
#include "connectors/connectoritem.h"
#include "items/moduleidnames.h"
#include "utils/bezier.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommandProgress::setActive(bool active) {
	m_active = active;
}

void CommandProgress::emitUndo() {
	Q_EMIT incUndo();
}

void CommandProgress::emitRedo() {
	Q_EMIT incRedo();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int SelectItemCommand::selectItemCommandID = 3;
int ChangeNoteTextCommand::changeNoteTextCommandID = 5;
int BaseCommand::nextIndex = 0;
CommandProgress BaseCommand::m_commandProgress;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BaseCommand::BaseCommand(BaseCommand::CrossViewType crossViewType, SketchWidget* sketchWidget, QUndoCommand *parent)
	: QUndoCommand(parent),
	m_crossViewType(crossViewType),
	m_sketchWidget(sketchWidget),
	m_parentCommand(parent),
	m_index(BaseCommand::nextIndex++)
{
}

BaseCommand::~BaseCommand() {
	Q_FOREACH (BaseCommand * baseCommand, m_commands) {
		delete baseCommand;
	}
	m_commands.clear();
}

BaseCommand::CrossViewType BaseCommand::crossViewType() const noexcept {
	return m_crossViewType;
}

void BaseCommand::setCrossViewType(BaseCommand::CrossViewType crossViewType) {
	m_crossViewType = crossViewType;
}

SketchWidget* BaseCommand::sketchWidget() const {
	return m_sketchWidget;
}

QString BaseCommand::getDebugString() const {
	return QString("%1 %2").arg(getParamString()).arg(text());
}

void BaseCommand::setUndoOnly() {
	m_undoOnly = true;
}

void BaseCommand::setRedoOnly()
{
	m_redoOnly = true;
}

void BaseCommand::setSkipFirstRedo() {
	m_skipFirstRedo = true;
}

QString BaseCommand::getParamString() const {
	return QString("%1 %2")
	       .arg(m_sketchWidget->viewName())
	       .arg((m_crossViewType == BaseCommand::SingleView) ? "single-view" : "cross-view");
}

int BaseCommand::subCommandCount() const {
	return m_commands.count();
}

const BaseCommand * BaseCommand::subCommand(int ix) const {
	if (ix < 0) return nullptr;
	if (ix >= m_commands.count()) return nullptr;

	return m_commands.at(ix);
}

void BaseCommand::addSubCommand(BaseCommand * subCommand) {
	m_commands.append(subCommand);
#ifndef QT_NO_DEBUG
	if (m_sketchWidget) {
		m_sketchWidget->undoStack()->writeUndo(subCommand, 4, this);
	}
#endif
}

const QUndoCommand * BaseCommand::parentCommand() const {
	return m_parentCommand;
}

void BaseCommand::subUndo() {
	for (int i = m_commands.count() - 1; i >= 0; i--) {
		m_commands[i]->undo();
	}
}

void BaseCommand::subRedo() {
	Q_FOREACH (BaseCommand * command, m_commands) {
		command->redo();
	}
}

void BaseCommand::subUndo(int index) {
	if (index < 0 || index >= m_commands.count()) return;

	m_commands[index]->undo();

}

void BaseCommand::subRedo(int index) {
	if (index < 0 || index >= m_commands.count()) return;

	m_commands[index]->redo();
}

int BaseCommand::index() const {
	return m_index;
}

void BaseCommand::undo() {
	if (m_commandProgress.active()) m_commandProgress.emitUndo();
}

void BaseCommand::redo() {
	if (m_commandProgress.active()) m_commandProgress.emitRedo();
}

CommandProgress * BaseCommand::initProgress() {
	m_commandProgress.setActive(true);
	return &m_commandProgress;
}

void BaseCommand::clearProgress() {
	m_commandProgress.setActive(false);
}

int BaseCommand::totalChildCount(const QUndoCommand * command) {
	int cc = command->childCount();
	int tcc = cc;
	for (int i = 0; i < cc; i++) {
		tcc += totalChildCount(command->child(i));
	}
	return tcc;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

AddDeleteItemCommand::AddDeleteItemCommand(SketchWidget* sketchWidget, BaseCommand::CrossViewType crossViewType, QString moduleID, ViewLayer::ViewLayerPlacement viewLayerPlacement, ViewGeometry & viewGeometry, qint64 id, long modelIndex, QHash<QString, QString> * localConnectors, QUndoCommand *parent)
	: SimulationCommand(crossViewType, sketchWidget, parent),
	m_moduleID(moduleID),
	m_itemID(id),
	m_viewGeometry(viewGeometry),
	m_modelIndex(modelIndex),
	m_dropOrigin(nullptr),
	m_viewLayerPlacement(viewLayerPlacement),
	m_localConnectors(localConnectors)
{
}

AddDeleteItemCommand::~AddDeleteItemCommand()
{
	if (m_localConnectors != nullptr) {
		delete m_localConnectors;
	}
}

QString AddDeleteItemCommand::getParamString() const {
	return BaseCommand::getParamString() +
	       QString(" moduleid:%1 id:%2 modelindex:%3 flags:%4")
	       .arg(m_moduleID)
	       .arg(m_itemID)
	       .arg(m_modelIndex)
	       .arg(m_viewGeometry.flagsAsInt());
}

long AddDeleteItemCommand::itemID() const {
	return m_itemID;
}

void AddDeleteItemCommand::setDropOrigin(SketchWidget * sketchWidget) {
	m_dropOrigin = sketchWidget;
}

SketchWidget * AddDeleteItemCommand::dropOrigin() {
	return m_dropOrigin;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SimulationCommand::SimulationCommand(BaseCommand::CrossViewType crossViewType, SketchWidget* sketchWidget, QUndoCommand *parent)
	: BaseCommand(crossViewType, sketchWidget, parent)
{
	m_mainWindow = dynamic_cast<MainWindow *>(sketchWidget->nativeParentWidget());
}

void SimulationCommand::undo() {
	BaseCommand::undo();
	if(m_mainWindow) {
		m_mainWindow->triggerSimulator();
	}
}

void SimulationCommand::redo() {
	BaseCommand::redo();
	if(m_mainWindow) {
		m_mainWindow->triggerSimulator();
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

AddItemCommand::AddItemCommand(SketchWidget* sketchWidget, BaseCommand::CrossViewType crossViewType, QString moduleID, ViewLayer::ViewLayerPlacement viewLayerPlacement, ViewGeometry & viewGeometry, qint64 id, bool updateInfoView, long modelIndex, QUndoCommand *parent)
	: AddDeleteItemCommand(sketchWidget, crossViewType, moduleID, viewLayerPlacement, viewGeometry, id, modelIndex, nullptr, parent),
	m_updateInfoView(updateInfoView),
	m_module(false),
	m_restoreIndexesCommand(nullptr)
{
}

void AddItemCommand::undo()
{
	m_sketchWidget->deleteItemForCommand(m_itemID, true, true, false);
	SimulationCommand::undo();
}

void AddItemCommand::redo()
{
	if (!m_skipFirstRedo) {
		m_sketchWidget->addItemForCommand(m_moduleID, m_viewLayerPlacement, m_crossViewType, m_viewGeometry, m_itemID, m_modelIndex, this);
	}
	m_skipFirstRedo = false;
	SimulationCommand::redo();
}

QString AddItemCommand::getParamString() const {
	return "AddItemCommand " + AddDeleteItemCommand::getParamString() +
	       QString(" loc:%1,%2 pt1:%3,%4 pt2:%5,%6")
	       .arg(m_viewGeometry.loc().x()).arg(m_viewGeometry.loc().y())
	       .arg(m_viewGeometry.line().p1().x()).arg(m_viewGeometry.line().p1().y())
	       .arg(m_viewGeometry.line().p2().x()).arg(m_viewGeometry.line().p2().y())
	       ;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DeleteItemCommand::DeleteItemCommand(SketchWidget* sketchWidget,BaseCommand::CrossViewType crossViewType,  QString moduleID, ViewLayer::ViewLayerPlacement viewLayerPlacement, ViewGeometry & viewGeometry, qint64 id, long modelIndex, QHash<QString, QString>* localConnectors, QUndoCommand *parent)
	: AddDeleteItemCommand(sketchWidget, crossViewType, moduleID, viewLayerPlacement, viewGeometry, id, modelIndex, localConnectors, parent)
{
}

void DeleteItemCommand::undo()
{
	auto * itemBase = m_sketchWidget->addItemForCommand(m_moduleID, m_viewLayerPlacement, m_crossViewType, m_viewGeometry, m_itemID, m_modelIndex, this);
	SimulationCommand::undo();
	if (m_localConnectors != nullptr && itemBase != nullptr) {
		auto * modelPart = itemBase->modelPart();
		if (modelPart != nullptr) {
			QString value = modelPart->properties().value("editable pin labels", "");
			if (value.compare("true") == 0) {
				for (auto id : m_localConnectors->keys()) {
					auto * connectorItem = itemBase->findConnectorItemWithSharedID(id);
					if (connectorItem != nullptr) {
						connectorItem->connector()->setConnectorLocalName(m_localConnectors->value(id));
					}
				}
				InfoGraphicsView * infoGraphicsView = InfoGraphicsView::getInfoGraphicsView(itemBase);
				if (infoGraphicsView != nullptr) {
					infoGraphicsView->changePinLabels(itemBase);
				}
			}
		}
	}

	BaseCommand::undo();
}

void DeleteItemCommand::redo()
{
	m_sketchWidget->deleteItemForCommand(m_itemID, true, m_crossViewType == BaseCommand::CrossView, false);
	SimulationCommand::redo();
}

QString DeleteItemCommand::getParamString() const {
	return "DeleteItemCommand " + AddDeleteItemCommand::getParamString();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

MoveItemCommand::MoveItemCommand(SketchWidget* sketchWidget, long itemID, ViewGeometry & oldG, ViewGeometry & newG, bool updateRatsnest, QUndoCommand *parent)
	: SimulationCommand(BaseCommand::SingleView, sketchWidget, parent),
	m_updateRatsnest(updateRatsnest),
	m_itemID(itemID),
	m_old(oldG),
	m_new(newG)
{
}

void MoveItemCommand::undo()
{
	m_sketchWidget->moveItemForCommand(m_itemID, m_old, m_updateRatsnest);
	SimulationCommand::undo();
}

void MoveItemCommand::redo()
{
	m_sketchWidget->moveItemForCommand(m_itemID, m_new, m_updateRatsnest);
	SimulationCommand::redo();
}

QString MoveItemCommand::getParamString() const {
	return QString("MoveItemCommand ")
	       + BaseCommand::getParamString() +
	       QString(" id:%1 old.x:%2 old.y:%3 old.px:%4 old.py:%5 new.x:%6 new.y:%7 new.px:%8 new.py:%9")
	       .arg(m_itemID)
	       .arg(m_old.loc().x())
	       .arg(m_old.loc().y())
	       .arg(m_old.line().p2().x())
	       .arg(m_old.line().p2().y())
	       .arg(m_new.loc().x())
	       .arg(m_new.loc().y())
	       .arg(m_new.line().p2().x())
	       .arg(m_new.line().p2().y())
	       ;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SimpleMoveItemCommand::SimpleMoveItemCommand(SketchWidget* sketchWidget, long itemID, QPointF & oldP, QPointF & newP, QUndoCommand *parent)
	: SimulationCommand(BaseCommand::SingleView, sketchWidget, parent),
	m_itemID(itemID),
	m_old(oldP),
	m_new(newP)
{
}

void SimpleMoveItemCommand::undo()
{
	m_sketchWidget->simpleMoveItemForCommand(m_itemID, m_old);
	SimulationCommand::undo();
}

void SimpleMoveItemCommand::redo()
{
	m_sketchWidget->simpleMoveItemForCommand(m_itemID, m_new);
	SimulationCommand::redo();
}

QString SimpleMoveItemCommand::getParamString() const {
	return QString("SimpleMoveItemCommand ")
	       + BaseCommand::getParamString() +
	       QString(" id:%1 old.x:%2 old.y:%3 new.x:%4 new.y:%5")
	       .arg(m_itemID)
	       .arg(m_old.x())
	       .arg(m_old.y())
	       .arg(m_new.x())
	       .arg(m_new.y())
	       ;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

MoveItemsCommand::MoveItemsCommand(SketchWidget* sketchWidget, bool updateRatsnest, QUndoCommand *parent)
	: SimulationCommand(BaseCommand::SingleView, sketchWidget, parent),
    m_updateRatsnest(updateRatsnest)
{
}

void MoveItemsCommand::undo()
{
	Q_FOREACH(MoveItemThing moveItemThing, m_items) {
		m_sketchWidget->moveItem(moveItemThing.id, moveItemThing.oldPos, m_updateRatsnest);
	}
	Q_FOREACH (long id, m_wires.keys()) {
		m_sketchWidget->updateWireForCommand(id, m_wires.value(id), m_updateRatsnest);
	}
	SimulationCommand::undo();
}

void MoveItemsCommand::redo()
{
	Q_FOREACH(MoveItemThing moveItemThing, m_items) {
		m_sketchWidget->moveItem(moveItemThing.id, moveItemThing.newPos, m_updateRatsnest);
	}
	Q_FOREACH (long id, m_wires.keys()) {
		m_sketchWidget->updateWireForCommand(id, m_wires.value(id), m_updateRatsnest);
	}
	SimulationCommand::redo();
}

void MoveItemsCommand::addWire(long id, const QString & connectorID)
{
	m_wires.insert(id, connectorID);
}

void MoveItemsCommand::addItem(long id, const QPointF & oldPos, const QPointF & newPos)
{
	MoveItemThing moveItemThing;
	moveItemThing.id = id;
	moveItemThing.oldPos = oldPos;
	moveItemThing.newPos = newPos;
	m_items.append(moveItemThing);
}

QString MoveItemsCommand::getParamString() const {
	return QString("MoveItemsCommand ")
	       + BaseCommand::getParamString() +
	       QString(" items:%1 wires:%2")
	       .arg(m_items.count())
	       .arg(m_wires.count())
	       ;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RotateItemCommand::RotateItemCommand(SketchWidget* sketchWidget, long itemID, double degrees, QUndoCommand *parent)
	: SimulationCommand(BaseCommand::SingleView, sketchWidget, parent),
	m_itemID(itemID),
	m_degrees(degrees)
{
}

void RotateItemCommand::undo()
{
	m_sketchWidget->rotateItemForCommand(m_itemID, -m_degrees);
	SimulationCommand::undo();
}

void RotateItemCommand::redo()
{
	m_sketchWidget->rotateItemForCommand(m_itemID, m_degrees);
	SimulationCommand::redo();
}

QString RotateItemCommand::getParamString() const {
	return QString("RotateItemCommand ")
	       + BaseCommand::getParamString() +
	       QString(" id:%1 by:%2")
	       .arg(m_itemID)
	       .arg(m_degrees);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlipItemCommand::FlipItemCommand(SketchWidget* sketchWidget, long itemID, Qt::Orientations orientation, QUndoCommand *parent)
	: BaseCommand(BaseCommand::SingleView, sketchWidget, parent),
	m_itemID(itemID),
	m_orientation(orientation)
{
}

void FlipItemCommand::undo()
{
	redo();
	BaseCommand::undo();
}

void FlipItemCommand::redo()
{
	m_sketchWidget->flipItemForCommand(m_itemID, m_orientation);
	BaseCommand::redo();
}


QString FlipItemCommand::getParamString() const {
	return QString("FlipItemCommand ")
	       + BaseCommand::getParamString() +
	       QString(" id:%1 by:%2")
	       .arg(m_itemID)
	       .arg(m_orientation);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ChangeConnectionCommand::ChangeConnectionCommand(SketchWidget * sketchWidget, BaseCommand::CrossViewType crossView,
        long fromID, const QString & fromConnectorID,
        long toID, const QString & toConnectorID,
        ViewLayer::ViewLayerPlacement viewLayerPlacement,
        bool connect, QUndoCommand * parent)
	: SimulationCommand(crossView, sketchWidget, parent)
{
	//DebugDialog::debug(QString("ccc: from %1 %2; to %3 %4, connect %5, layer %6").arg(fromID).arg(fromConnectorID).arg(toID).arg(toConnectorID).arg(connect).arg(viewLayerPlacement) );
	m_enabled = true;
	m_fromID = fromID;
	m_fromConnectorID = fromConnectorID;
	m_toID = toID;
	m_toConnectorID = toConnectorID;
	m_connect = connect;
	m_updateConnections = true;
	m_viewLayerPlacement = viewLayerPlacement;
}

void ChangeConnectionCommand::undo()
{
	if (m_enabled) {
		m_sketchWidget->changeConnection(m_fromID, m_fromConnectorID, m_toID, m_toConnectorID, m_viewLayerPlacement, !m_connect,  m_crossViewType == CrossView,  m_updateConnections);
		SimulationCommand::undo();
	}
}

void ChangeConnectionCommand::redo()
{
	if (m_enabled) {
		m_sketchWidget->changeConnection(m_fromID, m_fromConnectorID, m_toID, m_toConnectorID, m_viewLayerPlacement, m_connect,  m_crossViewType == CrossView, m_updateConnections);
		SimulationCommand::redo();
	}
}

void ChangeConnectionCommand::setUpdateConnections(bool updatem) {
	m_updateConnections = updatem;
}

void ChangeConnectionCommand::disable() {
	m_enabled = false;
}

QString ChangeConnectionCommand::getParamString() const {
	return QString("ChangeConnectionCommand ")
	       + BaseCommand::getParamString() +
	       QString(" fromid:%1 connid:%2 toid:%3 connid:%4 vlspec:%5 connect:%6")
	       .arg(m_fromID)
	       .arg(m_fromConnectorID)
	       .arg(m_toID)
	       .arg(m_toConnectorID)
	       .arg(m_viewLayerPlacement)
	       .arg(m_connect);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ChangeWireCommand::ChangeWireCommand(SketchWidget* sketchWidget, long fromID,
                                     const QLineF & oldLine, const QLineF & newLine, QPointF oldPos, QPointF newPos,
                                     bool updateConnections, bool updateRatsnest,
                                     QUndoCommand *parent)
	: SimulationCommand(BaseCommand::SingleView, sketchWidget, parent)
{
	m_updateRatsnest = updateRatsnest;
	m_fromID = fromID;
	m_oldLine = oldLine;
	m_newLine = newLine;
	m_oldPos = oldPos;
	m_newPos = newPos;
	m_updateConnections = updateConnections;
}

void ChangeWireCommand::undo()
{
	if (!m_redoOnly) {
		m_sketchWidget->changeWireForCommand(m_fromID, m_oldLine, m_oldPos, m_updateConnections, m_updateRatsnest);
	}
	SimulationCommand::undo();
}

void ChangeWireCommand::redo()
{
	if (!m_undoOnly) {
		m_sketchWidget->changeWireForCommand(m_fromID, m_newLine, m_newPos, m_updateConnections, m_updateRatsnest);
	}
	SimulationCommand::redo();
}

QString ChangeWireCommand::getParamString() const {
	return QString("ChangeWireCommand ")
	       + BaseCommand::getParamString() +
	       QString(" fromid:%1 oldp:%2,%3 newP:%4,%5 oldr:%7,%8,%9,%10 newr:%11,%12,%13,%14")
	       .arg(m_fromID)
	       .arg(m_oldPos.x()).arg(m_oldPos.y())
	       .arg(m_newPos.x()).arg(m_newPos.y())
	       .arg(m_oldLine.x1()).arg(m_oldLine.y1()).arg(m_oldLine.x2()).arg(m_oldLine.y2())
	       .arg(m_newLine.x1()).arg(m_newLine.y1()).arg(m_newLine.x2()).arg(m_newLine.y2())
	       ;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ChangeWireCurveCommand::ChangeWireCurveCommand(SketchWidget* sketchWidget, long fromID,
        const Bezier * oldBezier, const Bezier * newBezier, bool wasAutoroutable,
        QUndoCommand *parent)
	: SimulationCommand(BaseCommand::SingleView, sketchWidget, parent)
{
	m_fromID = fromID;
	m_wasAutoroutable = wasAutoroutable;
	m_oldBezier = m_newBezier = nullptr;
	if (oldBezier) {
		m_oldBezier = new Bezier;
		m_oldBezier->copy(oldBezier);
	}
	if (newBezier) {
		m_newBezier = new Bezier;
		m_newBezier->copy(newBezier);
	}
}

void ChangeWireCurveCommand::undo()
{
	if (!m_redoOnly) {
		m_sketchWidget->changeWireCurveForCommand(m_fromID, m_oldBezier, m_wasAutoroutable);
	}
	SimulationCommand::undo();
}

void ChangeWireCurveCommand::redo()
{
	if (!m_undoOnly) {
		if (m_skipFirstRedo) {
			m_skipFirstRedo = false;
		}
		else {
			m_sketchWidget->changeWireCurveForCommand(m_fromID, m_newBezier, false);
		}
	}
	SimulationCommand::redo();
}

QString ChangeWireCurveCommand::getParamString() const {
	QString oldBezier;
	QString newBezier;
	if (m_oldBezier) {
		oldBezier += QString("(%1,%2)").arg(m_oldBezier->cp0().x()).arg(m_oldBezier->cp0().y());
		oldBezier += QString("(%1,%2)").arg(m_oldBezier->cp1().x()).arg(m_oldBezier->cp1().y());
	}
	if (m_newBezier) {
		newBezier += QString("(%1,%2)").arg(m_newBezier->cp0().x()).arg(m_newBezier->cp0().y());
		newBezier += QString("(%1,%2)").arg(m_newBezier->cp1().x()).arg(m_newBezier->cp1().y());
	}

	return QString("ChangeWireCurveCommand ")
	       + BaseCommand::getParamString() +
	       QString(" fromid:%1 oldp:%2 newp:%3")
	       .arg(m_fromID)
	       .arg(oldBezier)
	       .arg(newBezier)
	       ;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ChangeLegCommand::ChangeLegCommand(SketchWidget* sketchWidget, long fromID, const QString & fromConnectorID,
                                   const QPolygonF & oldLeg, const QPolygonF & newLeg, bool relative, bool active,
                                   const QString & why, QUndoCommand *parent)
	: SimulationCommand(BaseCommand::SingleView, sketchWidget, parent),
	m_fromConnectorID(fromConnectorID),
	m_fromID(fromID),
	m_newLeg(newLeg),
	m_oldLeg(oldLeg),
	m_relative(relative),
	m_active(active),
	m_simple(false),
	m_why(why)
{
}

void ChangeLegCommand::undo()
{
	if (!m_redoOnly) {
		m_sketchWidget->changeLegForCommand(m_fromID, m_fromConnectorID, m_oldLeg, m_relative, m_why);
	}
	SimulationCommand::undo();
}

void ChangeLegCommand::setSimple()
{
	m_simple = true;
}

void ChangeLegCommand::redo()
{
	if (!m_undoOnly) {
		if (m_simple) {
			m_sketchWidget->changeLegForCommand(m_fromID, m_fromConnectorID, m_newLeg, m_relative, m_why);
		}
		else {
			m_sketchWidget->recalcLegForCommand(m_fromID, m_fromConnectorID, m_newLeg, m_relative, m_active, m_why);
		}
	}
	SimulationCommand::redo();
}

QString ChangeLegCommand::getParamString() const {

	QString oldLeg;
	QString newLeg;
	Q_FOREACH (QPointF p, m_oldLeg) {
		oldLeg += QString("(%1,%2)").arg(p.x()).arg(p.y());
	}
	Q_FOREACH (QPointF p, m_newLeg) {
		newLeg += QString("(%1,%2)").arg(p.x()).arg(p.y());
	}
	return QString("ChangeLegCommand ")
	       + BaseCommand::getParamString() +
	       QString(" fromid:%1 fromc:%2 %3 old:%4 new:%5")
	       .arg(m_fromID)
	       .arg(m_fromConnectorID)
	       .arg(m_why)
	       .arg(oldLeg)
	       .arg(newLeg)
	       ;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

MoveLegBendpointCommand::MoveLegBendpointCommand(SketchWidget* sketchWidget, long fromID, const QString & fromConnectorID,
        int index, QPointF oldPos, QPointF newPos, QUndoCommand *parent)
	: SimulationCommand(BaseCommand::SingleView, sketchWidget, parent)
{
	m_fromID = fromID;
	m_oldPos = oldPos;
	m_newPos = newPos;
	m_fromConnectorID = fromConnectorID;
	m_index = index;
}

void MoveLegBendpointCommand::undo()
{
	if (!m_redoOnly) {
		m_sketchWidget->moveLegBendpointForCommand(m_fromID, m_fromConnectorID, m_index, m_oldPos);
	}
	SimulationCommand::undo();
}

void MoveLegBendpointCommand::redo()
{
	if (!m_undoOnly) {
		m_sketchWidget->moveLegBendpointForCommand(m_fromID, m_fromConnectorID, m_index, m_newPos);
	}
	SimulationCommand::redo();
}

QString MoveLegBendpointCommand::getParamString() const {

	return QString("MoveLegBendpointCommand ")
	       + BaseCommand::getParamString() +
	       QString(" fromid:%1 fromc:%2 ix:%3 old:%4,%5 new:%6,%7")
	       .arg(m_fromID)
	       .arg(m_fromConnectorID)
	       .arg(m_index)
	       .arg(m_oldPos.x())
	       .arg(m_oldPos.y())
	       .arg(m_newPos.x())
	       .arg(m_newPos.y())
	       ;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ChangeLegCurveCommand::ChangeLegCurveCommand(SketchWidget* sketchWidget, long fromID, const QString & connectorID, int index,
        const Bezier * oldBezier, const Bezier * newBezier, QUndoCommand *parent)
	: SimulationCommand(BaseCommand::SingleView, sketchWidget, parent)
{
	m_fromID = fromID;
	m_oldBezier = m_newBezier = nullptr;
	if (oldBezier) {
		m_oldBezier = new Bezier;
		m_oldBezier->copy(oldBezier);
	}
	if (newBezier) {
		m_newBezier = new Bezier;
		m_newBezier->copy(newBezier);
	}

	m_fromConnectorID = connectorID;
	m_index = index;
}

void ChangeLegCurveCommand::undo()
{
	m_sketchWidget->changeLegCurveForCommand(m_fromID, m_fromConnectorID, m_index,  m_oldBezier);
	SimulationCommand::undo();
}

void ChangeLegCurveCommand::redo()
{
	if (m_skipFirstRedo) {
		m_skipFirstRedo = false;
	}
	else if (!m_undoOnly) {
		m_sketchWidget->changeLegCurveForCommand(m_fromID, m_fromConnectorID, m_index, m_newBezier);
	}
	SimulationCommand::redo();
}

QString ChangeLegCurveCommand::getParamString() const {
	QString oldBezier;
	QString newBezier;
	if (m_oldBezier) {
		oldBezier += QString("(%1,%2)").arg(m_oldBezier->cp0().x()).arg(m_oldBezier->cp0().y());
		oldBezier += QString("(%1,%2)").arg(m_oldBezier->cp1().x()).arg(m_oldBezier->cp1().y());
	}
	if (m_newBezier) {
		newBezier += QString("(%1,%2)").arg(m_newBezier->cp0().x()).arg(m_newBezier->cp0().y());
		newBezier += QString("(%1,%2)").arg(m_newBezier->cp1().x()).arg(m_newBezier->cp1().y());
	}

	return QString("ChangeLegCurveCommand ")
	       + BaseCommand::getParamString() +
	       QString(" fromid:%1 oldp:%2 newp:%3")
	       .arg(m_fromID)
	       .arg(oldBezier)
	       .arg(newBezier)
	       ;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ChangeLegBendpointCommand::ChangeLegBendpointCommand(SketchWidget* sketchWidget, long fromID, const QString & connectorID,
        int oldCount, int newCount, int index, QPointF pos,
        const Bezier * bezier0, const Bezier * bezier1, const Bezier * bezier2, QUndoCommand *parent)
	: BaseCommand(BaseCommand::SingleView, sketchWidget, parent)
{
	m_fromID = fromID;
	m_bezier0 = m_bezier1 = m_bezier2 = nullptr;
	if (bezier0) {
		m_bezier0 = new Bezier;
		m_bezier0->copy(bezier0);
	}
	if (bezier1) {
		m_bezier1 = new Bezier;
		m_bezier1->copy(bezier1);
	}
	if (bezier2) {
		m_bezier2 = new Bezier;
		m_bezier2->copy(bezier2);
	}

	m_fromConnectorID = connectorID;
	m_index = index;
	m_oldCount = oldCount;
	m_newCount = newCount;
	m_pos = pos;
}

void ChangeLegBendpointCommand::undo()
{
	if (m_newCount < m_oldCount) {
		m_sketchWidget->addLegBendpointForCommand(m_fromID, m_fromConnectorID, m_index, m_pos, m_bezier0, m_bezier1);
	}
	else {
		m_sketchWidget->removeLegBendpointForCommand(m_fromID, m_fromConnectorID, m_index, m_bezier0);
	}
	BaseCommand::undo();
}

void ChangeLegBendpointCommand::redo()
{
	if (m_skipFirstRedo) {
		m_skipFirstRedo = false;
	}
	else {
		if (m_newCount > m_oldCount) {
			m_sketchWidget->addLegBendpointForCommand(m_fromID, m_fromConnectorID, m_index, m_pos, m_bezier1, m_bezier2);
		}
		else {
			m_sketchWidget->removeLegBendpointForCommand(m_fromID, m_fromConnectorID, m_index, m_bezier2);
		}
	}
	BaseCommand::redo();
}

QString ChangeLegBendpointCommand::getParamString() const {
	QString bezier;
	if (m_bezier0) {
		bezier += QString("(%1,%2)").arg(m_bezier0->cp0().x()).arg(m_bezier0->cp0().y());
		bezier += QString("(%1,%2)").arg(m_bezier0->cp1().x()).arg(m_bezier0->cp1().y());
	}

	return QString("ChangeLegBendpointCommand ")
	       + BaseCommand::getParamString() +
	       QString(" fromid:%1 newp:%2")
	       .arg(m_fromID)
	       .arg(bezier)
	       ;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RotateLegCommand::RotateLegCommand(SketchWidget* sketchWidget, long fromID, const QString & fromConnectorID,
                                   const QPolygonF & oldLeg, bool active, QUndoCommand *parent)
	: SimulationCommand(BaseCommand::SingleView, sketchWidget, parent)
{
	m_fromID = fromID;
	m_oldLeg = oldLeg;
	m_fromConnectorID = fromConnectorID;
	m_active = active;
}

void RotateLegCommand::undo()
{
	SimulationCommand::undo();
}

void RotateLegCommand::redo()
{
	m_sketchWidget->rotateLegForCommand(m_fromID, m_fromConnectorID, m_oldLeg, m_active);
	SimulationCommand::redo();
}

QString RotateLegCommand::getParamString() const {

	QString oldLeg;
	Q_FOREACH (QPointF p, m_oldLeg) {
		oldLeg += QString("(%1,%2)").arg(p.x()).arg(p.y());
	}
	return QString("RotateLegCommand ")
	       + BaseCommand::getParamString() +
	       QString(" fromid:%1 fromc:%2 old:%3")
	       .arg(m_fromID)
	       .arg(m_fromConnectorID)
	       .arg(oldLeg)
	       ;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ChangeLayerCommand::ChangeLayerCommand(SketchWidget *sketchWidget, long fromID,
                                       double oldZ, double newZ,
                                       ViewLayer::ViewLayerID oldLayer, ViewLayer::ViewLayerID newLayer,
                                       QUndoCommand *parent)
	: BaseCommand(BaseCommand::SingleView, sketchWidget, parent)
{
	m_fromID = fromID;
	m_oldZ = oldZ;
	m_newZ = newZ;
	m_oldLayer = oldLayer;
	m_newLayer = newLayer;
}

void ChangeLayerCommand::undo()
{
	m_sketchWidget->changeLayerForCommand(m_fromID, m_oldZ, m_oldLayer);
	BaseCommand::undo();
}

void ChangeLayerCommand::redo()
{
	m_sketchWidget->changeLayerForCommand(m_fromID, m_newZ, m_newLayer);
	BaseCommand::redo();
}

QString ChangeLayerCommand::getParamString() const {
	return QString("ChangeLayerCommand ")
	       + BaseCommand::getParamString() +
	       QString(" fromid:%1 oldZ:%2 newZ:%3 oldL:%4 newL:%5")
	       .arg(m_fromID)
	       .arg(m_oldZ)
	       .arg(m_newZ)
	       .arg(m_oldLayer)
	       .arg(m_newLayer)
	       ;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SelectItemCommand::SelectItemCommand(SketchWidget* sketchWidget, SelectItemType type, QUndoCommand *parent)
	: BaseCommand(BaseCommand::CrossView, sketchWidget, parent),
	m_type(type),
	m_updated(false)
{
}

int SelectItemCommand::id() const {
	return selectItemCommandID;
}

void SelectItemCommand::setSelectItemType(SelectItemType type) {
	m_type = type;
}

void SelectItemCommand::copyUndo(SelectItemCommand * sother) {
	this->m_undoIDs.clear();
	for (long m_undoID : sother->m_undoIDs) {
		this->m_undoIDs.append(m_undoID);
	}
}

void SelectItemCommand::copyRedo(SelectItemCommand * sother) {
	this->m_redoIDs.clear();
	for (long m_redoID : sother->m_redoIDs) {
		this->m_redoIDs.append(m_redoID);
	}
}

void SelectItemCommand::clearRedo() {
	m_redoIDs.clear();
}

bool SelectItemCommand::mergeWith(const QUndoCommand *other)
{
	// "this" is earlier; "other" is later

	if (other->id() != id()) {
		return false;
	}

	const auto * sother = dynamic_cast<const SelectItemCommand *>(other);
	if (sother == nullptr) return false;

	if (sother->crossViewType() != this->crossViewType()) {
		return false;
	}

	this->m_redoIDs.clear();
	for (long m_redoID : sother->m_redoIDs) {
		this->m_redoIDs.append(m_redoID);
	}

	this->setText(sother->text());
	return true;
}

void SelectItemCommand::undo()
{
	selectAllFromStack(m_undoIDs, true, true);
	BaseCommand::undo();
}

void SelectItemCommand::redo()
{
	switch( m_type ) {
	case NormalSelect:
		selectAllFromStack(m_redoIDs, true, true);
		break;
	case NormalDeselect:
		selectAllFromStack(m_redoIDs, false, false);
		break;
	case SelectAll:
		m_sketchWidget->selectAllItems(true, m_crossViewType == BaseCommand::CrossView);
		break;
	case DeselectAll:
		m_sketchWidget->selectAllItems(false, m_crossViewType == BaseCommand::CrossView);
		break;
	}
	BaseCommand::redo();
}

void SelectItemCommand::selectAllFromStack(QList<long> & stack, bool select, bool updateInfoView) {
	m_sketchWidget->clearSelection();
	for (long i : stack) {
		m_sketchWidget->selectItemForCommand(i, select, updateInfoView, m_crossViewType == BaseCommand::CrossView);
	}
}

void SelectItemCommand::addUndo(long id) {
	m_undoIDs.append(id);
}

void SelectItemCommand::addRedo(long id) {
	if(m_type == NormalSelect) {
		m_redoIDs.append(id);
	}
}

QString SelectItemCommand::getParamString() const {
	return QString("SelectItemCommand ")
	       + BaseCommand::getParamString() +
	       QString(" type:%1")
	       .arg(m_type);
}

bool SelectItemCommand::updated() {
	return m_updated;
}


void SelectItemCommand::setUpdated(bool updated) {
	m_updated = updated;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ChangeZCommand::ChangeZCommand(SketchWidget* sketchWidget, QUndoCommand *parent)
	: BaseCommand(BaseCommand::SingleView, sketchWidget, parent)
{
}

ChangeZCommand::~ChangeZCommand() {
	Q_FOREACH (RealPair * realpair, m_triplets) {
		delete realpair;
	}
	m_triplets.clear();
}

void ChangeZCommand::addTriplet(long id, double oldZ, double newZ) {
	m_triplets.insert(id, new RealPair (oldZ, newZ));
}

void ChangeZCommand::undo()
{
	m_sketchWidget->changeZForCommand(m_triplets, first);
	BaseCommand::undo();
}

void ChangeZCommand::redo()
{
	m_sketchWidget->changeZForCommand(m_triplets, second);
	BaseCommand::redo();
}

double ChangeZCommand::first(RealPair * pair) {
	return pair->first;
}

double ChangeZCommand::second(RealPair * pair) {
	return pair->second;
}

QString ChangeZCommand::getParamString() const {
	return QString("ChangeZCommand ")
	       + BaseCommand::getParamString();
}

//////////////////////////////////////////singlev///////////////////////////////////////////////////////////////////////

CheckStickyCommand::CheckStickyCommand(SketchWidget* sketchWidget, BaseCommand::CrossViewType crossViewType, long itemID, bool checkCurrent, CheckType checkType, QUndoCommand *parent)
	: BaseCommand(crossViewType, sketchWidget, parent),
	m_itemID(itemID),
	m_checkCurrent(checkCurrent),
	m_checkType(checkType)
{
	m_skipFirstRedo = true;
}

CheckStickyCommand::~CheckStickyCommand() {
	Q_FOREACH (StickyThing * stickyThing, m_stickyList) {
		delete stickyThing;
	}

	m_stickyList.clear();
}

void CheckStickyCommand::undo()
{
	if (m_checkType == RedoOnly) return;

	Q_FOREACH (StickyThing * stickyThing, m_stickyList) {
		if (m_checkType == RemoveOnly) {
			stickyThing->sketchWidget->stickemForCommand(stickyThing->fromID, stickyThing->toID, !stickyThing->stickem);
		}
		else {
			stickyThing->sketchWidget->stickemForCommand(stickyThing->fromID, stickyThing->toID, stickyThing->stickem);
		}
	}
	BaseCommand::undo();
}

void CheckStickyCommand::redo()
{
	if (m_checkType == UndoOnly) return;

	if (m_skipFirstRedo) {
		m_sketchWidget->checkStickyForCommand(m_itemID, m_crossViewType == BaseCommand::CrossView, m_checkCurrent, this);
		m_skipFirstRedo = false;
	}
	else {
		Q_FOREACH (StickyThing * stickyThing, m_stickyList) {
			stickyThing->sketchWidget->stickemForCommand(stickyThing->fromID, stickyThing->toID, stickyThing->stickem);
		}
	}
	BaseCommand::redo();
}

QString CheckStickyCommand::getParamString() const {
	return QString("CheckStickyCommand ")
	       + BaseCommand::getParamString()
	       + QString("id:%1")
	       .arg(this->m_stickyList.count());
}

void CheckStickyCommand::stick(SketchWidget * sketchWidget, long fromID, long toID, bool stickem) {
	auto * stickyThing = new StickyThing;
	stickyThing->sketchWidget = sketchWidget;
	stickyThing->fromID = fromID;
	stickyThing->toID = toID;
	stickyThing->stickem = stickem;
	m_stickyList.append(stickyThing);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CleanUpWiresCommand::CleanUpWiresCommand(SketchWidget* sketchWidget, CleanUpWiresCommand::Direction direction, QUndoCommand *parent)
	: SimulationCommand(BaseCommand::CrossView, sketchWidget, parent),
	m_direction(direction)
{
}

void CleanUpWiresCommand::undo()
{
	Q_FOREACH (RatsnestConnectThing rct, m_ratsnestConnectThings) {
		m_sketchWidget->ratsnestConnectForCommand(rct.id, rct.connectorID, !rct.connect, true);
	}

	if (m_sketchWidgets.count() > 0)  {
		subUndo();
	}

	if (m_direction == UndoOnly) {
		m_sketchWidget->cleanUpWiresForCommand(m_crossViewType == BaseCommand::CrossView, nullptr);
	}
	SimulationCommand::undo();
}

void CleanUpWiresCommand::redo()
{
	Q_FOREACH (RatsnestConnectThing rct, m_ratsnestConnectThings) {
		m_sketchWidget->ratsnestConnectForCommand(rct.id, rct.connectorID, rct.connect, true);
	}

	if (m_sketchWidgets.count() > 0) {
		subRedo();
	}

	if (m_direction == RedoOnly) {
		m_sketchWidget->cleanUpWiresForCommand(m_crossViewType == BaseCommand::CrossView, this);
	}
	SimulationCommand::redo();
}

void CleanUpWiresCommand::addRatsnestConnect(long id, const QString & connectorID, bool connect)
{
	RatsnestConnectThing rct;
	rct.id = id;
	rct.connectorID = connectorID;
	rct.connect = connect;
	m_ratsnestConnectThings.append(rct);
}


void CleanUpWiresCommand::addRoutingStatus(SketchWidget * sketchWidget, const RoutingStatus & oldRoutingStatus, const RoutingStatus & newRoutingStatus)
{
	addSubCommand(new RoutingStatusCommand(sketchWidget, oldRoutingStatus, newRoutingStatus, nullptr));
}

void CleanUpWiresCommand::setDirection(CleanUpWiresCommand::Direction direction)
{
	m_direction = direction;
}

CleanUpWiresCommand::Direction CleanUpWiresCommand::direction()
{
	return m_direction;
}

void CleanUpWiresCommand::addTrace(SketchWidget * sketchWidget, Wire * wire)
{
	if (m_parentCommand) {
		for (int i = 0; i < m_parentCommand->childCount(); i++) {
			const auto * command = dynamic_cast<const DeleteItemCommand *>(m_parentCommand->child(i));

			if (command == nullptr) continue;
			if (command->itemID() == wire->id()) {
				return;
			}
		}
	}

	m_sketchWidgets.insert(sketchWidget);

	addSubCommand(new WireColorChangeCommand(sketchWidget, wire->id(), wire->colorString(), wire->colorString(), wire->opacity(), wire->opacity(), nullptr));
	addSubCommand(new WireWidthChangeCommand(sketchWidget, wire->id(), wire->width(), wire->width(), nullptr));

	Q_FOREACH (ConnectorItem * toConnectorItem, wire->connector0()->connectedToItems()) {
		addSubCommand(new ChangeConnectionCommand(sketchWidget, BaseCommand::CrossView, toConnectorItem->attachedToID(), toConnectorItem->connectorSharedID(),
		              wire->id(), "connector0",
		              ViewLayer::specFromID(wire->viewLayerID()),
		              false, nullptr));
	}
	Q_FOREACH (ConnectorItem * toConnectorItem, wire->connector1()->connectedToItems()) {
		addSubCommand(new ChangeConnectionCommand(sketchWidget, BaseCommand::CrossView, toConnectorItem->attachedToID(), toConnectorItem->connectorSharedID(),
		              wire->id(), "connector1",
		              ViewLayer::specFromID(wire->viewLayerID()),
		              false, nullptr));
	}

	addSubCommand(new DeleteItemCommand(sketchWidget, BaseCommand::CrossView, ModuleIDNames::WireModuleIDName, wire->viewLayerPlacement(), wire->getViewGeometry(), wire->id(), wire->modelPart()->modelIndex(), nullptr, nullptr));
}

bool CleanUpWiresCommand::hasTraces(SketchWidget * sketchWidget) {
	return m_sketchWidgets.contains(sketchWidget);
}

QString CleanUpWiresCommand::getParamString() const {
	return QString("CleanUpWiresCommand ")
	       + BaseCommand::getParamString()
	       + QString(" direction %1").arg(m_direction);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CleanUpRatsnestsCommand::CleanUpRatsnestsCommand(SketchWidget* sketchWidget, CleanUpWiresCommand::Direction direction, QUndoCommand *parent)
	: SimulationCommand(BaseCommand::CrossView, sketchWidget, parent)
{
	if (direction == CleanUpWiresCommand::UndoOnly) m_undoOnly = true;
	if (direction == CleanUpWiresCommand::RedoOnly) m_redoOnly = true;
}

void CleanUpRatsnestsCommand::undo()
{
	if (m_undoOnly) {
		m_sketchWidget->cleanupRatsnestsForCommand(true);
	}
	SimulationCommand::undo();
}

void CleanUpRatsnestsCommand::redo()
{
	if (m_redoOnly) {
		m_sketchWidget->cleanupRatsnestsForCommand(true);
	}
	SimulationCommand::redo();
}

QString CleanUpRatsnestsCommand::getParamString() const {
	return QString("CleanUpRatsnestsCommand ")
	       + BaseCommand::getParamString()
	       ;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

WireColorChangeCommand::WireColorChangeCommand(SketchWidget* sketchWidget, long wireId, const QString &oldColor, const QString &newColor, double oldOpacity, double newOpacity, QUndoCommand *parent)
	: BaseCommand(BaseCommand::SingleView, sketchWidget, parent),
	m_wireId(wireId),
	m_oldColor(oldColor),
	m_newColor(newColor),
	m_oldOpacity(oldOpacity),
	m_newOpacity(newOpacity)
{
}

void WireColorChangeCommand::undo() {
	m_sketchWidget->changeWireColorForCommand(m_wireId, m_oldColor, m_oldOpacity);
	BaseCommand::undo();
}

void WireColorChangeCommand::redo() {
	m_sketchWidget->changeWireColorForCommand(m_wireId, m_newColor, m_newOpacity);
	BaseCommand::redo();
}

QString WireColorChangeCommand::getParamString() const {
	return QString("WireColorChangeCommand ")
	       + BaseCommand::getParamString()
	       + QString(" id:%1 oldcolor:%2 oldop:%3 newcolor:%4 newop:%5")
	       .arg(m_wireId).arg(m_oldColor).arg(m_oldOpacity).arg(m_newColor).arg(m_newOpacity);

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

WireWidthChangeCommand::WireWidthChangeCommand(SketchWidget* sketchWidget, long wireId, double oldWidth, double newWidth, QUndoCommand *parent)
	: BaseCommand(BaseCommand::SingleView, sketchWidget, parent),
	m_wireId(wireId),
	m_oldWidth(oldWidth),
	m_newWidth(newWidth)
{
}

void WireWidthChangeCommand::undo() {
	m_sketchWidget->changeWireWidthForCommand(m_wireId, m_oldWidth);
	BaseCommand::undo();
}

void WireWidthChangeCommand::redo() {
	m_sketchWidget->changeWireWidthForCommand(m_wireId, m_newWidth);
	BaseCommand::redo();
}


QString WireWidthChangeCommand::getParamString() const {
	return QString("WireWidthChangeCommand ")
	       + BaseCommand::getParamString()
	       + QString(" id:%1 oldw:%2 neww:%3")
	       .arg(m_wireId).arg(m_oldWidth).arg(m_newWidth);

}

///////////////////////////////////////////

RoutingStatusCommand::RoutingStatusCommand(SketchWidget * sketchWidget, const RoutingStatus & oldRoutingStatus, const RoutingStatus & newRoutingStatus, QUndoCommand * parent)
	: BaseCommand(BaseCommand::SingleView, sketchWidget, parent),
	m_oldRoutingStatus(oldRoutingStatus),
	m_newRoutingStatus(newRoutingStatus)
{
}

void RoutingStatusCommand::undo() {
	m_sketchWidget->forwardRoutingStatusForCommand(m_oldRoutingStatus);
	BaseCommand::undo();
}

void RoutingStatusCommand::redo() {
	m_sketchWidget->forwardRoutingStatusForCommand(m_newRoutingStatus);
	BaseCommand::redo();
}

QString RoutingStatusCommand::getParamString() const {
	return QString("RoutingStatusCommand ")
	       + BaseCommand::getParamString()
	       + QString(" oldnet:%1 oldnetrouted:%2 oldconnectors:%3 oldjumpers:%4 newnet:%51 newnetrouted:%6 newconnectors:%7 newjumpers:%8 ")
	       .arg(m_oldRoutingStatus.m_netCount).arg(m_oldRoutingStatus.m_netRoutedCount).arg(m_oldRoutingStatus.m_connectorsLeftToRoute).arg(m_oldRoutingStatus.m_jumperItemCount)
	       .arg(m_newRoutingStatus.m_netCount).arg(m_newRoutingStatus.m_netRoutedCount).arg(m_newRoutingStatus.m_connectorsLeftToRoute).arg(m_newRoutingStatus.m_jumperItemCount);

}

///////////////////////////////////////////////

ShowLabelFirstTimeCommand::ShowLabelFirstTimeCommand(SketchWidget *sketchWidget, CrossViewType crossView, long id, bool oldVis, bool newVis, QUndoCommand *parent)
	: BaseCommand(crossView, sketchWidget, parent),
	m_itemID(id),
	m_oldVis(oldVis),
	m_newVis(newVis)
{
}

void ShowLabelFirstTimeCommand::undo()
{
	BaseCommand::undo();
}

void ShowLabelFirstTimeCommand::redo()
{
	m_sketchWidget->showLabelFirstTimeForCommand(m_itemID, m_newVis, true);
	BaseCommand::redo();
}

QString ShowLabelFirstTimeCommand::getParamString() const {
	return QString("ShowLabelFirstTimeCommand ")
	       + BaseCommand::getParamString()
	       + QString(" id:%1 %2 %3")
	       .arg(m_itemID).arg(m_oldVis).arg(m_newVis);

}

///////////////////////////////////////////////

RestoreLabelCommand::RestoreLabelCommand(SketchWidget *sketchWidget,long id, QDomElement & oldLabelGeometry, QDomElement & newLabelGeometry, QUndoCommand *parent)
	: BaseCommand(BaseCommand::SingleView, sketchWidget, parent),
	m_itemID(id),
	m_oldLabelGeometry(oldLabelGeometry),
	m_newLabelGeometry(newLabelGeometry)
{
	// seems to be safe to keep a copy of the element even though the document may no longer exist
}

void RestoreLabelCommand::undo()
{
	m_sketchWidget->restorePartLabelForCommand(m_itemID, m_oldLabelGeometry);
	BaseCommand::undo();
}

void RestoreLabelCommand::redo()
{
	m_sketchWidget->restorePartLabelForCommand(m_itemID, m_newLabelGeometry);
	BaseCommand::redo();
}

QString RestoreLabelCommand::getParamString() const {
	return QString("RestoreLabelCommand ")
	       + BaseCommand::getParamString()
	       + QString(" id:%1")
	       .arg(m_itemID);

}

///////////////////////////////////////////////

CheckPartLabelLayerVisibilityCommand::CheckPartLabelLayerVisibilityCommand(SketchWidget *sketchWidget,long id, QUndoCommand *parent)
	: BaseCommand(BaseCommand::SingleView, sketchWidget, parent),
	m_itemID(id)
{
}

void CheckPartLabelLayerVisibilityCommand::undo()
{
	m_sketchWidget->checkPartLabelLayerVisibilityForCommand(m_itemID);
	BaseCommand::undo();
}

void CheckPartLabelLayerVisibilityCommand::redo()
{
	m_sketchWidget->checkPartLabelLayerVisibilityForCommand(m_itemID);
	BaseCommand::redo();
}

QString CheckPartLabelLayerVisibilityCommand::getParamString() const {
	return QString("CheckPartLabelLayerVisibilityCommand ")
	       + BaseCommand::getParamString()
	       + QString(" id:%1")
	       .arg(m_itemID);

}

///////////////////////////////////////////////

MoveLabelCommand::MoveLabelCommand(SketchWidget *sketchWidget, long id, QPointF oldPos, QPointF oldOffset, QPointF newPos, QPointF newOffset, QUndoCommand *parent)
	: BaseCommand(BaseCommand::SingleView, sketchWidget, parent),
	m_itemID(id),
	m_oldPos(oldPos),
	m_newPos(newPos),
	m_oldOffset(oldOffset),
	m_newOffset(newOffset)
{
}

void MoveLabelCommand::undo()
{
	m_sketchWidget->movePartLabelForCommand(m_itemID, m_oldPos, m_oldOffset);
	BaseCommand::undo();
}

void MoveLabelCommand::redo()
{
	m_sketchWidget->movePartLabelForCommand(m_itemID, m_newPos, m_newOffset);
	BaseCommand::redo();
}


QString MoveLabelCommand::getParamString() const {
	return QString("MoveLabelCommand ")
	       + BaseCommand::getParamString()
	       + QString(" id:%1")
	       .arg(m_itemID);

}

///////////////////////////////////////////////

MoveLockCommand::MoveLockCommand(SketchWidget *sketchWidget, long id, bool oldLock, bool newLock, QUndoCommand *parent)
	: BaseCommand(BaseCommand::SingleView, sketchWidget, parent),
	m_itemID(id),
	m_oldLock(oldLock),
	m_newLock(newLock)
{
}

void MoveLockCommand::undo()
{
	m_sketchWidget->setMoveLockForCommand(m_itemID, m_oldLock);
	BaseCommand::undo();
}

void MoveLockCommand::redo()
{
	m_sketchWidget->setMoveLockForCommand(m_itemID, m_newLock);
	BaseCommand::redo();
}


QString MoveLockCommand::getParamString() const {
	return QString("MoveLockCommand ")
	       + BaseCommand::getParamString()
	       + QString(" id:%1 o:%2 n:%3")
	       .arg(m_itemID)
	       .arg(m_oldLock)
	       .arg(m_newLock);

}

///////////////////////////////////////////////

ChangeLabelTextCommand::ChangeLabelTextCommand(SketchWidget *sketchWidget, long id,
        const QString & oldText, const QString & newText,
        QUndoCommand *parent)
	: BaseCommand(BaseCommand::CrossView, sketchWidget, parent),
	m_itemID(id),
	m_oldText(oldText),
	m_newText(newText)
{
}

void ChangeLabelTextCommand::undo() {
	m_sketchWidget->setInstanceTitleForCommand(m_itemID, m_newText, m_oldText, false, true);
	BaseCommand::undo();
}

void ChangeLabelTextCommand::redo() {
	m_sketchWidget->setInstanceTitleForCommand(m_itemID, m_oldText, m_newText, false, true);
	BaseCommand::redo();
}

QString ChangeLabelTextCommand::getParamString() const {
	return QString("ChangeLabelTextCommand ")
	       + BaseCommand::getParamString()
	       + QString(" id:%1 old:%2 new:%3")
	       .arg(m_itemID).arg(m_oldText).arg(m_newText);

}

///////////////////////////////////////////////

IncLabelTextCommand::IncLabelTextCommand(SketchWidget *sketchWidget, long id,  QUndoCommand *parent)
	: BaseCommand(BaseCommand::CrossView, sketchWidget, parent),
	m_itemID(id)
{
}

void IncLabelTextCommand::undo() {
	// only used when creating new parts via paste
	BaseCommand::undo();
}

void IncLabelTextCommand::redo() {
	if (!m_skipFirstRedo) {
		m_sketchWidget->incInstanceTitleForCommand(m_itemID);
	}
	BaseCommand::redo();
}

QString IncLabelTextCommand::getParamString() const {
	return QString("IncLabelTextCommand ")
	       + BaseCommand::getParamString()
	       + QString(" id:%1")
	       .arg(m_itemID);

}///////////////////////////////////////////////

ChangeNoteTextCommand::ChangeNoteTextCommand(SketchWidget *sketchWidget, long id,
        const QString & oldText, const QString & newText,
        QSizeF oldSize, QSizeF newSize, QUndoCommand *parent)
	: BaseCommand(BaseCommand::CrossView, sketchWidget, parent),
	m_itemID(id),
	m_oldText(oldText),
	m_newText(newText),
	m_oldSize(oldSize),
	m_newSize(newSize)
{
}

void ChangeNoteTextCommand::undo() {
	m_sketchWidget->setNoteTextForCommand(m_itemID, m_oldText);
	if (m_oldSize != m_newSize) {
		m_sketchWidget->resizeNoteForCommand(m_itemID, m_oldSize);
	}
	BaseCommand::undo();
}

void ChangeNoteTextCommand::redo() {
	if (m_skipFirstRedo) {
		m_skipFirstRedo = false;
		return;
	}

	m_sketchWidget->setNoteTextForCommand(m_itemID, m_newText);
	if (m_oldSize != m_newSize) {
		m_sketchWidget->resizeNoteForCommand(m_itemID, m_newSize);
	}
	BaseCommand::redo();
}

int ChangeNoteTextCommand::id() const {
	return changeNoteTextCommandID;
}

bool ChangeNoteTextCommand::mergeWith(const QUndoCommand *other)
{
	// "this" is earlier; "other" is later

	if (other->id() != id()) {
		return false;
	}

	const auto * sother = dynamic_cast<const ChangeNoteTextCommand *>(other);
	if (sother == nullptr) return false;

	if (sother->m_itemID != m_itemID) {
		// this is not the same label so don't merge
		return false;
	}

	m_newSize = sother->m_newSize;
	m_newText = sother->m_newText;
	return true;
}

QString ChangeNoteTextCommand::getParamString() const {
	return QString("ChangeNoteTextCommand ")
	       + BaseCommand::getParamString()
	       + QString(" id:%1 old:%2 new:%3")
	       .arg(m_itemID).arg(m_oldText).arg(m_newText);

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RotateFlipLabelCommand::RotateFlipLabelCommand(SketchWidget* sketchWidget, long itemID, double degrees, Qt::Orientations orientation, QUndoCommand *parent)
	: BaseCommand(BaseCommand::SingleView, sketchWidget, parent),
	m_itemID(itemID),
	m_degrees(degrees),
	m_orientation(orientation)
{
}

void RotateFlipLabelCommand::undo()
{
	m_sketchWidget->rotateFlipPartLabelForCommand(m_itemID, -m_degrees, m_orientation);
	BaseCommand::undo();
}

void RotateFlipLabelCommand::redo()
{
	m_sketchWidget->rotateFlipPartLabelForCommand(m_itemID, m_degrees, m_orientation);
	BaseCommand::redo();
}

QString RotateFlipLabelCommand::getParamString() const {
	return QString("RotateFlipLabelCommand ")
	       + BaseCommand::getParamString()
	       + QString(" id:%1 degrees:%2 orientation:%3")
	       .arg(m_itemID).arg(m_degrees).arg(m_orientation);

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ResizeNoteCommand::ResizeNoteCommand(SketchWidget* sketchWidget, long itemID, const QSizeF & oldSize, const QSizeF & newSize, QUndoCommand *parent)
	: BaseCommand(BaseCommand::SingleView, sketchWidget, parent),
	m_itemID(itemID),
	m_oldSize(oldSize),
	m_newSize(newSize)
{
}

void ResizeNoteCommand::undo()
{
	m_sketchWidget->resizeNoteForCommand(m_itemID, m_oldSize);
	BaseCommand::undo();
}

void ResizeNoteCommand::redo()
{
	m_sketchWidget->resizeNoteForCommand(m_itemID, m_newSize);
	BaseCommand::redo();
}

QString ResizeNoteCommand::getParamString() const {
	return QString("ResizeNoteCommand ")
	       + BaseCommand::getParamString()
	       + QString(" id:%1 oldsz:%2 %3 newsz:%4 %5")
	       .arg(m_itemID).arg(m_oldSize.width()).arg(m_oldSize.height()).arg(m_newSize.width()).arg(m_newSize.height());

}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ResizeBoardCommand::ResizeBoardCommand(SketchWidget * sketchWidget, long itemID, double oldWidth, double oldHeight, double newWidth, double newHeight, QUndoCommand * parent)
	: BaseCommand(BaseCommand::SingleView, sketchWidget, parent),
	m_oldWidth(oldWidth),
	m_oldHeight(oldHeight),
	m_newWidth(newWidth),
	m_newHeight(newHeight),
	m_itemID(itemID)
{
}

void ResizeBoardCommand::undo() {
	if (!m_redoOnly) {
		m_sketchWidget->resizeBoard(m_itemID, m_oldWidth, m_oldHeight);
	}
	BaseCommand::undo();
}

void ResizeBoardCommand::redo() {
	if (!m_undoOnly) {
		m_sketchWidget->resizeBoard(m_itemID, m_newWidth, m_newHeight);
	}
	BaseCommand::redo();
}

QString ResizeBoardCommand::getParamString() const {

	return QString("ResizeBoardCommand ")
	       + BaseCommand::getParamString() +
	       QString(" id:%1 ow:%2 oh:%3 nw:%4 nh:%5")
	       .arg(m_itemID)
	       .arg(m_oldWidth)
	       .arg(m_oldHeight)
	       .arg(m_newWidth)
	       .arg(m_newHeight);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TransformItemCommand::TransformItemCommand(SketchWidget *sketchWidget, long id, const QTransform & oldMatrix, const QTransform & newMatrix, QUndoCommand *parent)
	: SimulationCommand(BaseCommand::SingleView, sketchWidget, parent),
	m_itemID(id),
	m_oldMatrix(oldMatrix),
	m_newMatrix(newMatrix)
{
}

void TransformItemCommand::undo()
{
	m_sketchWidget->transformItemForCommand(m_itemID, m_oldMatrix);
	SimulationCommand::undo();
}

void TransformItemCommand::redo()
{
	m_sketchWidget->transformItemForCommand(m_itemID, m_newMatrix);
	SimulationCommand::redo();
}

QString TransformItemCommand::getParamString() const {
	return QString("TransformItemCommand ")
	       + BaseCommand::getParamString() +
	       QString(" id:%1")
	       .arg(m_itemID);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SetResistanceCommand::SetResistanceCommand(SketchWidget * sketchWidget, long itemID, QString oldResistance, QString newResistance, QString oldPinSpacing, QString newPinSpacing, QUndoCommand * parent)
	: SimulationCommand(BaseCommand::CrossView, sketchWidget, parent),
	m_oldResistance(oldResistance),
	m_newResistance(newResistance),
	m_oldPinSpacing(oldPinSpacing),
	m_newPinSpacing(newPinSpacing),
	m_itemID(itemID)
{
}

void SetResistanceCommand::undo() {
	m_sketchWidget->setResistance(m_itemID, m_oldResistance, m_oldPinSpacing, true);
	SimulationCommand::undo();
}

void SetResistanceCommand::redo() {
	m_sketchWidget->setResistance(m_itemID, m_newResistance, m_newPinSpacing, true);
	SimulationCommand::redo();
}

QString SetResistanceCommand::getParamString() const {

	return QString("SetResistanceCommand ")
	       + BaseCommand::getParamString() +
	       QString(" id:%1 ov:%2 nv:%3")
	       .arg(m_itemID)
	       .arg(m_oldResistance)
	       .arg(m_newResistance);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SetPropCommand::SetPropCommand(SketchWidget * sketchWidget, long itemID, QString prop, QString oldValue, QString newValue, bool redraw, QUndoCommand * parent)
	: SimulationCommand(BaseCommand::CrossView, sketchWidget, parent),
	m_redraw(redraw),
	m_prop(prop),
	m_oldValue(oldValue),
	m_newValue(newValue),
	m_itemID(itemID)
{
}

void SetPropCommand::undo() {
	m_sketchWidget->setProp(m_itemID, m_prop, m_oldValue, m_redraw, true);
	SimulationCommand::undo();
}

void SetPropCommand::redo() {
	m_sketchWidget->setProp(m_itemID, m_prop, m_newValue, m_redraw, true);
	SimulationCommand::redo();
}

QString SetPropCommand::getParamString() const {

	return QString("SetPropCommand ")
	       + BaseCommand::getParamString() +
	       QString(" id:%1 p:%2 o:%3 n:%4")
	       .arg(m_itemID)
	       .arg(m_prop)
	       .arg(m_oldValue)
	       .arg(m_newValue);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ResizeJumperItemCommand::ResizeJumperItemCommand(SketchWidget * sketchWidget, long itemID, QPointF oldPos, QPointF oldC0, QPointF oldC1, QPointF newPos, QPointF newC0, QPointF newC1, QUndoCommand * parent)
	: BaseCommand(BaseCommand::SingleView, sketchWidget, parent),
	m_oldPos(oldPos),
	m_oldC0(oldC0),
	m_oldC1(oldC1),
	m_newPos(newPos),
	m_newC0(newC0),
	m_newC1(newC1),
	m_itemID(itemID)
{
}

void ResizeJumperItemCommand::undo() {
	m_sketchWidget->resizeJumperItem(m_itemID, m_oldPos, m_oldC0, m_oldC1);
	BaseCommand::undo();
}

void ResizeJumperItemCommand::redo() {
	m_sketchWidget->resizeJumperItem(m_itemID, m_newPos, m_newC0, m_newC1);
	BaseCommand::redo();
}

QString ResizeJumperItemCommand::getParamString() const {

	return QString("ResizeJumperItemCommand ")
	       + BaseCommand::getParamString() +
	       QString(" id:%1 op:%2,%3 oc0:%4,%5 oc1:%6,%7 np:%8,%9 nc0:%10,%11 nc1:%12,%13")
	       .arg(m_itemID)
	       .arg(m_oldPos.x()).arg(m_oldPos.y())
	       .arg(m_oldC0.x()).arg(m_oldC0.y())
	       .arg(m_oldC1.x()).arg(m_oldC1.y())
	       .arg(m_newPos.x()).arg(m_newPos.y())
	       .arg(m_newC0.x()).arg(m_newC0.y())
	       .arg(m_newC1.x()).arg(m_newC1.y())
	       ;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ShowLabelCommand::ShowLabelCommand(SketchWidget *sketchWidget, QUndoCommand *parent)
	: BaseCommand(BaseCommand::SingleView, sketchWidget, parent)
{
}

void ShowLabelCommand::undo()
{
	Q_FOREACH (long id, m_idStates.keys()) {
		m_sketchWidget->showPartLabelForCommand(id, (m_idStates.value(id) & 2) != 0);
	}
	BaseCommand::undo();
}

void ShowLabelCommand::redo()
{
	Q_FOREACH (long id, m_idStates.keys()) {
		m_sketchWidget->showPartLabelForCommand(id, (m_idStates.value(id) & 1) != 0);
	}
	BaseCommand::redo();
}

void ShowLabelCommand::add(long id, bool prev, bool post)
{
	int v = 0;
	if (prev) v += 2;
	if (post) v += 1;
	m_idStates.insert(id, v);
}


QString ShowLabelCommand::getParamString() const {

	return QString("ShowLabelCommand ")
	       + BaseCommand::getParamString();
}

///////////////////////////////////////////////

LoadLogoImageCommand::LoadLogoImageCommand(SketchWidget *sketchWidget, long id,
        const QString & oldSvg, const QSizeF oldAspectRatio, const QString & oldFilename,
        const QString & newFilename, bool addName, QUndoCommand *parent)
	: BaseCommand(BaseCommand::SingleView, sketchWidget, parent),
	m_itemID(id),
	m_oldSvg(oldSvg),
	m_oldAspectRatio(oldAspectRatio),
	m_oldFilename(oldFilename),
	m_newFilename(newFilename),
	m_addName(addName)
{
}

void LoadLogoImageCommand::undo() {
	if (!m_redoOnly) {
		m_sketchWidget->loadLogoImage(m_itemID, m_oldSvg, m_oldAspectRatio, m_oldFilename);
	}
	BaseCommand::undo();
}

void LoadLogoImageCommand::redo() {
	if (m_newFilename.isEmpty()) {
	}
	else if (!m_undoOnly) {
		m_sketchWidget->loadLogoImage(m_itemID, m_newFilename, m_addName);
	}
	BaseCommand::redo();
}

QString LoadLogoImageCommand::getParamString() const {
	return QString("LoadLogoImageCommand ")
	       + BaseCommand::getParamString()
	       + QString(" id:%1 old:%2 new:%3")
	       .arg(m_itemID).arg(m_oldFilename).arg(m_newFilename);

}

///////////////////////////////////////////////

ChangeBoardLayersCommand::ChangeBoardLayersCommand(SketchWidget *sketchWidget, int oldLayers, int newLayers, QUndoCommand *parent)
	: BaseCommand(BaseCommand::SingleView, sketchWidget, parent),
	m_oldLayers(oldLayers),
	m_newLayers(newLayers)
{
}

void ChangeBoardLayersCommand::undo() {
	m_sketchWidget->changeBoardLayers(m_oldLayers, true);
	for (int i = childCount() - 1; i >= 0; i--) {
		((QUndoCommand *) child(i))->undo();
	}
	BaseCommand::undo();
}

void ChangeBoardLayersCommand::redo() {
	m_sketchWidget->changeBoardLayers(m_newLayers, true);
	for (int i = 0; i < childCount(); i++) {
		((QUndoCommand *) child(i))->redo();
	}
	BaseCommand::redo();
}

QString ChangeBoardLayersCommand::getParamString() const {
	return QString("ChangeBoardLayersCommand ")
	       + BaseCommand::getParamString()
	       + QString(" old:%1 new:%2")
	       .arg(m_oldLayers).arg(m_newLayers);

}


///////////////////////////////////////////////

SetDropOffsetCommand::SetDropOffsetCommand(SketchWidget *sketchWidget, long id,  QPointF dropOffset, QUndoCommand *parent)
	: BaseCommand(BaseCommand::CrossView, sketchWidget, parent),
	m_itemID(id),
	m_dropOffset(dropOffset)
{
}

void SetDropOffsetCommand::undo() {
	// only used when creating new parts
	BaseCommand::undo();
}

void SetDropOffsetCommand::redo() {
	m_sketchWidget->setItemDropOffsetForCommand(m_itemID, m_dropOffset);
	BaseCommand::redo();

}

QString SetDropOffsetCommand::getParamString() const {
	return QString("SetDropOffsetCommand ")
	       + BaseCommand::getParamString()
	       + QString(" id:%1 %2,%3")
	       .arg(m_itemID).arg(m_dropOffset.x()).arg(m_dropOffset.y());

}

///////////////////////////////////////////////

RenamePinsCommand::RenamePinsCommand(SketchWidget *sketchWidget, long id, const QStringList & oldOnes, const QStringList & newOnes, QUndoCommand *parent) :
	BaseCommand(BaseCommand::CrossView, sketchWidget, parent),
	m_itemID(id),
	m_oldLabels(oldOnes),
	m_newLabels(newOnes)
{
}

void RenamePinsCommand::undo() {
	m_sketchWidget->renamePinsForCommand(m_itemID, m_oldLabels);
	BaseCommand::undo();
}

void RenamePinsCommand::redo() {
	m_sketchWidget->renamePinsForCommand(m_itemID, m_newLabels);
	BaseCommand::redo();
}

QString RenamePinsCommand::getParamString() const {
	return QString("RenamePinsCommand ")
	       + BaseCommand::getParamString()
	       + QString(" id:%1")
	       .arg(m_itemID);

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

GroundFillSeedCommand::GroundFillSeedCommand(SketchWidget* sketchWidget, QUndoCommand *parent)
	: BaseCommand(BaseCommand::SingleView, sketchWidget, parent)
{
	setText(QObject::tr("Set Ground Fill Seed"));
}

void GroundFillSeedCommand::undo()
{
	Q_FOREACH(GFSThing gfsThing, m_items) {
		m_sketchWidget->setGroundFillSeedForCommand(gfsThing.id, gfsThing.connectorID, !gfsThing.seed);
	}
	BaseCommand::undo();
}

void GroundFillSeedCommand::redo()
{
	Q_FOREACH(GFSThing gfsThing, m_items) {
		m_sketchWidget->setGroundFillSeedForCommand(gfsThing.id, gfsThing.connectorID, gfsThing.seed);
	}
	BaseCommand::redo();
}

void GroundFillSeedCommand::addItem(long id, const QString & connectorID, bool seed)
{
	GFSThing gfsThing;
	gfsThing.id = id;
	gfsThing.connectorID = connectorID;
	gfsThing.seed = seed;
	m_items.append(gfsThing);
}

QString GroundFillSeedCommand::getParamString() const {
	return QString("GroundFillSeedCommand ")
	       + BaseCommand::getParamString() +
	       QString(" items:%1")
	       .arg(m_items.count())
	       ;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

WireExtrasCommand::WireExtrasCommand(SketchWidget* sketchWidget, long fromID,
                                     const QDomElement & oldExtras, const QDomElement & newExtras,
                                     QUndoCommand *parent)
	: BaseCommand(BaseCommand::SingleView, sketchWidget, parent),
	m_fromID(fromID),
	m_oldExtras(oldExtras),
	m_newExtras(newExtras)
{

}

void WireExtrasCommand::undo()
{
	if (!m_redoOnly) {
		m_sketchWidget->setWireExtrasForCommand(m_fromID, m_oldExtras);
	}
	BaseCommand::undo();
}

void WireExtrasCommand::redo()
{
	if (!m_undoOnly) {
		m_sketchWidget->setWireExtrasForCommand(m_fromID, m_newExtras);
	}
	BaseCommand::redo();
}

QString WireExtrasCommand::getParamString() const {
	return QString("WireExtrasCommand ")
	       + BaseCommand::getParamString() +
	       QString(" fromid:%1 ")
	       .arg(m_fromID)
	       ;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

HidePartLayerCommand::HidePartLayerCommand(SketchWidget *sketchWidget, long fromID,
        ViewLayer::ViewLayerID layerID, bool wasHidden, bool isHidden,
        QUndoCommand *parent)
	: BaseCommand(BaseCommand::SingleView, sketchWidget, parent),
	m_fromID(fromID),
	m_wasHidden(wasHidden),
	m_isHidden(isHidden),
	m_layerID(layerID),
	m_oldLayer(ViewLayer::ViewLayerID::UnknownLayer)
{
}

void HidePartLayerCommand::undo()
{
	m_sketchWidget->hidePartLayerForCommand(m_fromID, m_layerID, m_wasHidden);
	BaseCommand::undo();
}

void HidePartLayerCommand::redo()
{
	m_sketchWidget->hidePartLayerForCommand(m_fromID, m_layerID, m_isHidden);
	BaseCommand::redo();
}

QString HidePartLayerCommand::getParamString() const {
	return QString("HidePartLayerCommand ")
	       + BaseCommand::getParamString() +
	       QString(" fromid:%1 l:%2 was:%3 is:%4")
	       .arg(m_fromID)
	       .arg(m_layerID)
	       .arg(m_wasHidden)
	       .arg(m_isHidden)
	       ;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

AddSubpartCommand::AddSubpartCommand(SketchWidget *sketchWidget,  CrossViewType crossView, long id, long subpartID, QUndoCommand *parent)
	: BaseCommand(crossView, sketchWidget, parent),
    m_itemID(id),
    m_subpartItemID(subpartID)
{

}

void AddSubpartCommand::undo()
{
	if (!m_redoOnly) {
		m_sketchWidget->removeSubpartForCommand(m_itemID, m_subpartItemID, crossViewType() == BaseCommand::CrossView);
	}
	BaseCommand::undo();
}

void AddSubpartCommand::redo()
{
	if (!m_undoOnly) {
		m_sketchWidget->addSubpartForCommand(m_itemID, m_subpartItemID, crossViewType() == BaseCommand::CrossView);
	}
	BaseCommand::redo();
}

QString AddSubpartCommand::getParamString() const {
	return QString("AddSubpartCommand ")
	       + BaseCommand::getParamString() +
	       QString(" id:%1 subpart id:%2")
	       .arg(m_itemID)
	       .arg(m_subpartItemID)
	       ;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RemoveSubpartCommand::RemoveSubpartCommand(SketchWidget *sketchWidget,  CrossViewType crossView, long id, long subpartID, QUndoCommand *parent)
	: BaseCommand(crossView, sketchWidget, parent),
    m_itemID(id),
    m_subpartItemID(subpartID)
{

}

void RemoveSubpartCommand::undo()
{
	if (!m_redoOnly) {
		m_sketchWidget->addSubpartForCommand(m_itemID, m_subpartItemID, crossViewType() == BaseCommand::CrossView);
	}
	BaseCommand::undo();
}

void RemoveSubpartCommand::redo()
{
	if (!m_undoOnly) {
		m_sketchWidget->removeSubpartForCommand(m_itemID, m_subpartItemID, crossViewType() == BaseCommand::CrossView);
	}
	BaseCommand::redo();
}

QString RemoveSubpartCommand::getParamString() const {
	return QString("RemoveSubpartCommand ")
	       + BaseCommand::getParamString() +
	       QString(" id:%1 subpart id:%2")
	       .arg(m_itemID)
	       .arg(m_subpartItemID)
	       ;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

PackItemsCommand::PackItemsCommand(SketchWidget *sketchWidget, int columns, const QList<long> & ids, QUndoCommand *parent)
	: BaseCommand(BaseCommand::CrossView, sketchWidget, parent),
	m_columns(columns),
	m_ids(ids),
	m_firstTime(true)
{
}

void PackItemsCommand::undo()
{
	BaseCommand::undo();
}

void PackItemsCommand::redo()
{
	if (m_firstTime) {
		m_sketchWidget->packItemsForCommand(m_columns, m_ids, m_parentCommand, true);
		m_firstTime = false;
	}

	BaseCommand::redo();
}

QString PackItemsCommand::getParamString() const {
	return QString("PackItemsCommand ")
	       + BaseCommand::getParamString() +
	       QString(" columns:%1 count:%2")
	       .arg(m_columns)
	       .arg(m_ids.count())
	       ;
}

////////////////////////////////////

TemporaryCommand::TemporaryCommand(const QString & text) : QUndoCommand(text), m_enabled(true) { }

TemporaryCommand::~TemporaryCommand() { }

void TemporaryCommand::setEnabled(bool enabled) {
	m_enabled = enabled;
}

void TemporaryCommand::redo() {
	if (m_enabled) {
		QUndoCommand::redo();
	}
}
