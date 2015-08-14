/*
   For more information, please see: http://software.sci.utah.edu

   The MIT License

   Copyright (c) 2015 Scientific Computing and Imaging Institute,
   University of Utah.

   License for the specific language governing rights and limitations under
   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included
   in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.
*/

#include <iostream>
#include <QtGui>
#include <boost/lambda/lambda.hpp>
#include <Dataflow/Network/Port.h>
#include <Interface/Application/Port.h>
#include <Interface/Application/GuiLogger.h>
#include <Interface/Application/Connection.h>
#include <Interface/Application/PositionProvider.h>
#include <Interface/Application/Utility.h>
#include <Interface/Application/ClosestPortFinder.h>
#include <Core/Application/Application.h>
#include <Dataflow/Engine/Controller/NetworkEditorController.h>
#include <Core/Application/Preferences/Preferences.h>
#include <Interface/Application/SCIRunMainWindow.h>

using namespace SCIRun::Gui;
using namespace SCIRun::Core;
using namespace SCIRun::Dataflow::Networks;


namespace SCIRun {
  namespace Gui {

    bool portTypeMatches(const std::string& portTypeToMatch, bool isInput, const ModuleDescription& module)
    {
      if (isInput)
        return std::find_if(module.output_ports_.begin(), module.output_ports_.end(), [&](const OutputPortDescription& out) { return out.datatype == portTypeToMatch; }) != module.output_ports_.end();
      else
        return std::find_if(module.input_ports_.begin(), module.input_ports_.end(), [&](const InputPortDescription& in) { return in.datatype == portTypeToMatch; }) != module.input_ports_.end();
    }

    QList<QAction*> fillMenu(QMenu* menu, const ModuleDescriptionMap& moduleMap, PortWidget* parent)
    {
      const std::string& portTypeToMatch = parent->get_typename();
      bool isInput = parent->isInput();
      return fillMenuWithFilteredModuleActions(menu, moduleMap,
        [=](const ModuleDescription& m) { return portTypeMatches(portTypeToMatch, isInput, m); },
        [=](QAction* action) { QObject::connect(action, SIGNAL(triggered()), parent, SLOT(connectNewModule())); },
        parent);
    }

    //TODO: lots of duplicated filtering here. Make smarter logic to cache based on port type, since it's the same menu for each type--just need to copy an existing one.
    // parent is passed to fix menu positioning--every child menu needs the same (graphicsscene button) parent!
    QList<QAction*> fillMenuWithFilteredModuleActions(QMenu* menu, const ModuleDescriptionMap& moduleMap, ModulePredicate modulePred,
      QActionHookup hookup, QWidget* parent)
    {
      QList<QAction*> allCompatibleActions;
      for (const auto& package : moduleMap)
      {
        const std::string& packageName = package.first;

        QList<QMenu*> packageMenus;
        for (const auto& category : package.second)
        {
          const std::string& categoryName = category.first;
          QList<QAction*> actions;

          for (const auto& module : category.second)
          {
            if (modulePred(module.second))
            {
              const std::string& moduleName = module.first;
              auto qname = QString::fromStdString(moduleName);
              auto action = new QAction(qname, menu);
              hookup(action);
              actions.append(action);
              allCompatibleActions.append(action);
            }
          }
          if (!actions.empty())
          {
            auto m = new QMenu(QString::fromStdString(categoryName), parent);
            m->addActions(actions);
            packageMenus.append(m);
          }
        }
        if (!packageMenus.isEmpty())
        {
          auto p = new QMenu(QString::fromStdString(packageName), parent);
          for (QMenu* menu : packageMenus)
            p->addMenu(menu);

          menu->addMenu(p);
          menu->addSeparator();
        }
      }
      return allCompatibleActions;
    }


    class PortActionsMenu : public QMenu
    {
    public:
      explicit PortActionsMenu(PortWidget* parent) : QMenu("Actions", parent), faves_(nullptr)
      {
        QList<QAction*> actions;
        if (!parent->isInput())
        {
          auto pc = new QAction("Port Caching", parent);
          pc->setCheckable(true);
          connect(pc, SIGNAL(triggered(bool)), parent, SLOT(portCachingChanged(bool)));
          //TODO for now: disable
          pc->setEnabled(false);
          //TODO:
          //pc->setChecked(parent->getCached())...or something
          actions.append(pc);
          actions.append(separatorAction(parent));
        }
        addActions(actions);

        auto m = new QMenu("Connect Module", parent);
        faves_ = new QMenu("Favorites", parent);
        m->addMenu(faves_);
        compatibleModuleActions_ = fillMenu(m, Core::Application::Instance().controller()->getAllAvailableModuleDescriptions(), parent);
        addMenu(m);
      }
      void filterFavorites()
      {
        faves_->clear();
        for (const auto& action : compatibleModuleActions_)
          if (SCIRunMainWindow::Instance()->isInFavorites(action->text())) // TODO: break out predicate
            faves_->addAction(action);
      }
    private:
      QMenu* faves_;
      QList<QAction*> compatibleModuleActions_;
    };
  }}

PortWidget::PortWidgetMap PortWidget::portWidgetMap_;
PortWidget::PotentialConnectionMap PortWidget::potentialConnectionsMap_;

PortWidgetBase::PortWidgetBase(QWidget* parent) : QPushButton(parent), isHighlighted_(false) {}

PortWidget::PortWidget(const QString& name, const QColor& color, const std::string& datatype, const ModuleId& moduleId,
  const PortId& portId, size_t index,
  bool isInput, bool isDynamic,
  boost::shared_ptr<ConnectionFactory> connectionFactory,
  boost::shared_ptr<ClosestPortFinder> closestPortFinder,
  PortDataDescriber portDataDescriber,
  QWidget* parent /* = 0 */)
  : PortWidgetBase(parent),
  name_(name), moduleId_(moduleId), portId_(portId), index_(index), color_(color), typename_(datatype), isInput_(isInput), isDynamic_(isDynamic), isConnected_(false), lightOn_(false), currentConnection_(0),
  connectionFactory_(connectionFactory),
  closestPortFinder_(closestPortFinder),
  menu_(new PortActionsMenu(this)),
  portDataDescriber_(portDataDescriber)
{
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  setAcceptDrops(true);
  setToolTip(QString(name_).replace("_", " ") + (isDynamic ? ("[" + QString::number(portId_.id) + "]") : "") + " : " + QString::fromStdString(typename_));

  setMenu(menu_);

  portWidgetMap_[moduleId_.id_][isInput_][portId_] = this;
}

PortWidget::~PortWidget()
{
  portWidgetMap_[moduleId_.id_][isInput_].erase(portId_);
}

QSize PortWidgetBase::sizeHint() const
{
  const int width = DEFAULT_WIDTH;
  const int coloredHeight = isInput() ? 5 : 4;
  const int blackHeight = 2;
  QSize size(width, coloredHeight + blackHeight);
  const double highlightFactor = 1.7;
  if (isHighlighted_)
  {
    size *= highlightFactor;
  }
  return size;
}

void PortWidget::toggleLight()
{
  lightOn_ = !lightOn_;
}

void PortWidget::turn_off_light()
{
  lightOn_ = false;
}

void PortWidget::turn_on_light()
{
  lightOn_ = true;
}

void PortWidgetBase::paintEvent(QPaintEvent* event)
{
  QSize size = sizeHint();
  resize(size);

  QPainter painter(this);
  painter.fillRect(QRect(QPoint(), size), color());
  QPoint lightStart = isInput() ? QPoint(0, size.height() - 2) : QPoint(0,0);

  //TODO: remove light entirely?
  QColor lightColor = isLightOn() ? Qt::red : color();

  painter.fillRect(QRect(lightStart, QSize(size.width(), 2)), lightColor);
  setFixedHeight(size.height());
}

void PortWidget::mousePressEvent(QMouseEvent* event)
{
  doMousePress(event->button(), event->pos());
}

void PortWidget::doMousePress(Qt::MouseButton button, const QPointF& pos)
{
  if (button == Qt::LeftButton && !isConnected())
  {
    toggleLight();
    startPos_ = pos;
    update();
  }
  else
  {
    //qDebug() << "mouse press sth else";
  }
}

void PortWidget::mouseMoveEvent(QMouseEvent* event)
{
  doMouseMove(event->buttons(), event->pos());
}

void PortWidget::doMouseMove(Qt::MouseButtons buttons, const QPointF& pos)
{
  if (buttons & Qt::LeftButton && (!isConnected() || !isInput()))
  {
    int distance = (pos - startPos_).manhattanLength();
    if (distance >= QApplication::startDragDistance())
      dragImpl(pos);
  }
  else
  {
    //qDebug() << "mouse move sth else";
  }
}

void PortWidget::mouseReleaseEvent(QMouseEvent* event)
{
  doMouseRelease(event->button(), event->pos(), event->modifiers());
}

void PortWidget::doMouseRelease(Qt::MouseButton button, const QPointF& pos, Qt::KeyboardModifiers modifiers)
{
  if (!isInput() && (button == Qt::MiddleButton || modifiers & Qt::ControlModifier))
  {
    DataInfoDialog::show(getPortDataDescriber(), "Port", moduleId_.id_ + "::" + portId_.toString());
  }
  else if (button == Qt::LeftButton)
  {
    toggleLight();
    update();

    if (currentConnection_)
    {
      makeConnection(pos);
    }
  }
  else if (button == Qt::RightButton && (!isConnected() || !isInput()))
  {
    menu_->filterFavorites();
    showMenu();
  }
  else
  {
    //qDebug() << "mouse release sth else";
  }
}

size_t PortWidget::getIndex() const
{
  return index_;
}

PortId PortWidget::id() const
{
  return portId_;
}

void PortWidget::setIndex(size_t index)
{
  index_ = index;
}

namespace
{
  //const int PORT_CONNECTION_THRESHOLD = 12;
  const int NEW_PORT_CONNECTION_THRESHOLD = 900;
  const int VERY_CLOSE_TO_DESTINATION_PORT_CONNECTION_THRESHOLD = 30;
  const int TOO_CLOSE_TO_SOURCE_PORT_CONNECTION_THRESHOLD = 100;
}

namespace SCIRun {
  namespace Gui {
    struct DeleteCurrentConnectionAtEndOfBlock
    {
      explicit DeleteCurrentConnectionAtEndOfBlock(PortWidget* p) : p_(p) {}
      ~DeleteCurrentConnectionAtEndOfBlock()
      {
        p_->cancelConnectionsInProgress();
      }
      PortWidget* p_;
    };
  }
}

void PortWidget::cancelConnectionsInProgress()
{
  delete currentConnection_;
  currentConnection_ = 0;
}

void PortWidget::makeConnection(const QPointF& pos)
{
  DeleteCurrentConnectionAtEndOfBlock deleter(this);  //GUI concern: could go away if we got a NO-CONNECT signal from service layer

  auto connection = std::find_if(potentialConnections_.begin(), potentialConnections_.end(), [](const ConnectionInProgress* c) { return c->isHighlighted();});
  if (connection != potentialConnections_.end())
  {
    tryConnectPort(pos, (*connection)->receiver(), std::numeric_limits<double>::max());
  }
  else
  {
    //qDebug() << "no highlighted port found";
  }
  clearPotentialConnections();

#if 0 // clean up later, might reuse closestPortFinder
  else //old way
  {
    auto port = closestPortFinder_->closestPort(pos);  //GUI concern: needs unit test
    if (port)
      tryConnectPort(pos, port, PORT_CONNECTION_THRESHOLD);
  }
#endif
}

void PortWidget::clearPotentialConnections()
{
  potentialConnectionsMap_[this].clear();
  for (auto& c : potentialConnections_)
    delete c;
  potentialConnections_.clear();
}

void PortWidget::tryConnectPort(const QPointF& pos, PortWidget* port, double threshold)
{
  int distance = (pos - port->position()).manhattanLength();     //GUI concern: needs unit test
  if (distance <= threshold)                 //GUI concern: needs unit test
  {
    Q_EMIT requestConnection(this, port);
  }
}

void PortWidget::MakeTheConnection(const SCIRun::Dataflow::Networks::ConnectionDescription& cd)
{
  if (matches(cd))
  {
    auto out = portWidgetMap_[cd.out_.moduleId_][false][cd.out_.portId_];
    auto in = portWidgetMap_[cd.in_.moduleId_][true][cd.in_.portId_];
    auto id = SCIRun::Dataflow::Networks::ConnectionId::create(cd);
    auto c = connectionFactory_->makeFinishedConnection(out, in, id);
    connect(c, SIGNAL(deleted(const SCIRun::Dataflow::Networks::ConnectionId&)), this, SIGNAL(connectionDeleted(const SCIRun::Dataflow::Networks::ConnectionId&)));
    connect(c, SIGNAL(noteChanged()), this, SIGNAL(connectionNoteChanged()));
    connect(out, SIGNAL(portMoved()), c, SLOT(trackNodes()));
    connect(in, SIGNAL(portMoved()), c, SLOT(trackNodes()));
    setConnected(true);
  }
}

void PortWidget::setPositionObject(PositionProviderPtr provider)
{
  NeedsScenePositionProvider::setPositionObject(provider);
  Q_EMIT portMoved();
}

void PortWidget::moveEvent(QMoveEvent * event)
{
  QPushButton::moveEvent(event);
  Q_EMIT portMoved();
}

bool PortWidget::matches(const SCIRun::Dataflow::Networks::ConnectionDescription& cd) const
{
  return (isInput() && cd.in_.moduleId_ == moduleId_ && cd.in_.portId_ == portId_)
    || (!isInput() && cd.out_.moduleId_ == moduleId_ && cd.out_.portId_ == portId_);
}

bool PortWidget::sharesParentModule(const PortWidget& other) const
{
  return moduleId_ == other.moduleId_;
}

bool PortWidget::isFullInputPort() const
{
  return isInput() && !connections_.empty();
}

void PortWidget::dragImpl(const QPointF& endPos)
{
  if (!currentConnection_)
  {
    currentConnection_ = connectionFactory_->makeConnectionInProgress(this);
  }
  currentConnection_->update(endPos);

  auto isCompatible = [this](const PortWidget* port)
  {
    PortConnectionDeterminer q;
    return q.canBeConnected(*port, *this);
  };

  forEachPort([this](PortWidget* p) { this->makePotentialConnectionLine(p); }, isCompatible);

  if (!potentialConnections_.empty())
  {
    auto minPotential = *std::min_element(potentialConnections_.begin(), potentialConnections_.end(),
      [&](const ConnectionInProgress* a, const ConnectionInProgress* b)
    { return (endPos - a->endpoint()).manhattanLength() < (endPos - b->endpoint()).manhattanLength(); });

    for (const auto& pc : potentialConnections_)
    {
      pc->highlight(false);
    }

    auto distToPotentialPort = (endPos - minPotential->endpoint()).manhattanLength();
    if (distToPotentialPort < VERY_CLOSE_TO_DESTINATION_PORT_CONNECTION_THRESHOLD
      ||
      (distToPotentialPort < NEW_PORT_CONNECTION_THRESHOLD
      && (endPos - position()).manhattanLength() > TOO_CLOSE_TO_SOURCE_PORT_CONNECTION_THRESHOLD))  // "deciding not to connect" threshold
    {
      minPotential->highlight(true);
    }
  }
}

template <typename Func, typename Pred>
void PortWidget::forEachPort(Func func, Pred pred)
{
  for (auto& p1 : portWidgetMap_)
  {
    for (auto& p2 : p1.second)
    {
      for (auto& p3 : p2.second)
      {
        if (pred(p3.second))
          func(p3.second);
      }
    }
  }
}

void PortWidget::makePotentialConnectionLine(PortWidget* other)
{
  auto potentials = potentialConnectionsMap_[this];
  if (potentials.find(other) == potentials.end())
  {
    potentialConnectionsMap_[this][other] = true;
    auto potential = connectionFactory_->makePotentialConnection(this);
    potential->update(other->position());
    potential->setReceiver(other);
    potentialConnections_.insert(potential);
  }
}

void PortWidget::addConnection(ConnectionLine* c)
{
  setConnected(true);
  connections_.insert(c);
}

void PortWidget::removeConnection(ConnectionLine* c)
{
  disconnect(c);
  connections_.erase(c);
  if (connections_.empty())
    setConnected(false);
}

void PortWidget::deleteConnections()
{
  Q_FOREACH (ConnectionLine* c, connections_)
    delete c;
  connections_.clear();
  setConnected(false);
}

void PortWidget::trackConnections()
{
  Q_FOREACH (ConnectionLine* c, connections_)
    c->trackNodes();
}

QPointF PortWidget::position() const
{
  if (positionProvider_)
  {
    return positionProvider_->currentPosition();
  }
  return pos();
}

size_t PortWidget::nconnections() const
{
  return connections_.size();
}

std::string PortWidget::get_typename() const
{
  return typename_;
}

std::string PortWidget::get_portname() const
{
  return name_.toStdString();
}

ModuleId PortWidget::getUnderlyingModuleId() const
{
  return moduleId_;
}

void PortWidget::setHighlight(bool on, bool individual)
{
  if (!isHighlighted_ && on && individual)
    Q_EMIT highlighted(true);

  isHighlighted_ = on;
  if (on)
  {
    if (isInput() && isConnected())
      isHighlighted_ = false;
  }
  else
  {
    Q_EMIT highlighted(false);
  }
}

void PortWidget::portCachingChanged(bool checked)
{
  //TODO
  std::cout << "Port " << moduleId_.id_ << "::" << name().toStdString() << " Caching turned " << (checked ? "on." : "off.") << std::endl;
}

void PortWidget::connectNewModule()
{
  QAction* action = qobject_cast<QAction*>(sender());
  QString moduleToAddName = action->text();
  Q_EMIT connectNewModule(this, moduleToAddName.toStdString());
}

InputPortWidget::InputPortWidget(const QString& name, const QColor& color, const std::string& datatype,
  const ModuleId& moduleId, const PortId& portId, size_t index, bool isDynamic,
  boost::shared_ptr<ConnectionFactory> connectionFactory,
  boost::shared_ptr<ClosestPortFinder> closestPortFinder,
  PortDataDescriber portDataDescriber,
  QWidget* parent /* = 0 */)
  : PortWidget(name, color, datatype, moduleId, portId, index, true, isDynamic, connectionFactory, closestPortFinder, portDataDescriber, parent)
{
}

OutputPortWidget::OutputPortWidget(const QString& name, const QColor& color, const std::string& datatype,
  const ModuleId& moduleId, const PortId& portId, size_t index, bool isDynamic,
  boost::shared_ptr<ConnectionFactory> connectionFactory,
  boost::shared_ptr<ClosestPortFinder> closestPortFinder,
  PortDataDescriber portDataDescriber,
  QWidget* parent /* = 0 */)
  : PortWidget(name, color, datatype, moduleId, portId, index, false, isDynamic, connectionFactory, closestPortFinder, portDataDescriber, parent)
{
}

BlankPort::BlankPort(QWidget* parent) : PortWidgetBase(parent) {}

PortId BlankPort::id() const
{
  return PortId(0, "<Blank>");
}

ModuleId BlankPort::getUnderlyingModuleId() const
{
  return ModuleId("<Blank>");
}

QColor BlankPort::color() const
{
  return QColor(0,0,0,0);
}

std::vector<PortWidget*> PortWidget::connectedPorts() const
{
  std::vector<PortWidget*> otherPorts;
  auto notThisOne = [this](const std::pair<PortWidget*, PortWidget*>& portPair) { if (portPair.first == this) return portPair.second; else return portPair.first; };
  for (const auto& c : connections_)
  {
    auto ends = c->connectedPorts();

    otherPorts.push_back(notThisOne(ends));
  }
  return otherPorts;
}
