/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "DeviceIllustration.h"
#include "PersonalStyle.h"

#include <QApplication>
#include <QFocusEvent>
#include <QComboBox>
#include <QToolButton>
#include <QSignalBlocker>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <array>
#include <cmath>

namespace deskflow::gui::personal {
namespace {
using Direction = layout::Direction;
constexpr qreal diagramWidth = 600;
constexpr qreal diagramHeight = 280;
const QPointF origin(300, 140);
const std::array<Direction, 4> directions{Direction::Left, Direction::Right, Direction::Above, Direction::Below};

QPointF offset(Direction direction)
{
  switch (direction) {
  case Direction::Left: return {-195, 0};
  case Direction::Right: return {195, 0};
  case Direction::Above: return {0, -97};
  case Direction::Below: return {0, 97};
  default: return {};
  }
}

QRectF nodeRect(const QPointF &center) { return QRectF(center - QPointF(76, 42), QSizeF(152, 84)); }

QString directionName(Direction direction)
{
  switch (direction) {
  case Direction::Left: return QObject::tr("左侧");
  case Direction::Right: return QObject::tr("右侧");
  case Direction::Above: return QObject::tr("上方");
  case Direction::Below: return QObject::tr("下方");
  default: return QObject::tr("位置未确认");
  }
}
} // namespace

DeviceIllustration::DeviceIllustration(QWidget *parent) : QWidget(parent)
{
  setObjectName(QStringLiteral("screenLayoutIllustration"));
  setMinimumHeight(250);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  m_screenPicker = new QComboBox(this);
  m_screenPicker->setObjectName(QStringLiteral("screenLayoutDevicePicker"));
  m_screenPicker->setAccessibleName(tr("选择要移动的电脑"));
  m_screenPicker->setToolTip(tr("选择电脑后，点击图中设备或按 Tab 聚焦画布，再用方向键移动"));
  connect(m_screenPicker, &QComboBox::currentTextChanged, this, &DeviceIllustration::selectScreen);
  const auto button = [this](const QString &text, const QString &name, const QString &accessible) {
    auto *item = new QToolButton(this);
    item->setText(text); item->setObjectName(name); item->setAccessibleName(accessible); item->setToolTip(accessible);
    item->setAutoRaise(true); item->setFocusPolicy(Qt::StrongFocus); return item;
  };
  m_zoomOut = button(QStringLiteral("−"), QStringLiteral("screenLayoutZoomOut"), tr("缩小屏幕布局"));
  m_zoomIn = button(QStringLiteral("+"), QStringLiteral("screenLayoutZoomIn"), tr("放大屏幕布局"));
  m_fit = button(tr("适合窗口"), QStringLiteral("screenLayoutFit"), tr("将全部电脑适合窗口"));
  connect(m_fit, &QToolButton::clicked, this, &DeviceIllustration::resetView);
  connect(m_zoomIn, &QToolButton::clicked, this, [this] { m_zoom = qMin(4.0, m_zoom * 1.25); update(); });
  connect(m_zoomOut, &QToolButton::clicked, this, [this] { m_zoom = qMax(0.4, m_zoom / 1.25); update(); });
  updateCanvasControls();
  refreshAccessibility();
}

QSize DeviceIllustration::sizeHint() const { return {600, 280}; }

void DeviceIllustration::setDeviceContext(shortcuts::DeviceOS sourceOS, bool server)
{
  m_sourceOS = server ? shortcuts::localOS() : sourceOS;
  m_serverMode = server;
  refreshAccessibility();
  update();
}

void DeviceIllustration::setConnection(bool connected, const QString &host, const QString &macName)
{
  m_connected = connected;
  m_host = host;
  m_macName = macName;
  refreshAccessibility();
  update();
}

void DeviceIllustration::setScreenLayout(const layout::Snapshot &snapshot, bool pending, const QString &feedback)
{
  const bool changed = m_layout != snapshot;
  const bool topologyChanged = m_layout.screens != snapshot.screens;
  const bool editChanged = hasTopology() || !snapshot.screens.isEmpty()
      ? (topologyChanged || m_layout.revision != snapshot.revision || m_layout.editable != snapshot.editable)
      : changed;
  if (editChanged || m_pending != pending) cancelDrag();
  m_layout = snapshot;
  if (changed) m_canvasFeedback.clear();
  if (topologyChanged) { m_pan = {}; m_zoom = 1; }
  updateCanvasControls();
  m_pending = pending;
  m_feedback = feedback;
  refreshAccessibility();
  update();
}

void DeviceIllustration::setConfigurationBusy(bool busy)
{
  m_busy = busy;
  if (busy) cancelDrag();
  refreshAccessibility();
  update();
}

bool DeviceIllustration::canEdit() const
{
  return isEnabled() && m_layout.editable && (hasTopology() || layout::isCardinal(m_layout.direction)) && !m_pending && !m_busy;
}

void DeviceIllustration::refreshAccessibility()
{
  const auto source = m_layout.serverName.isEmpty() ? tr("键鼠来源") : m_layout.serverName;
  const auto client = m_layout.clientName.isEmpty() ? tr("接收电脑") : m_layout.clientName;
  if (hasTopology()) {
    QString position;
    for (const auto &screen : m_layout.screens)
      if (screen.name == m_selectedScreen)
        position = tr("选中 %1，第 %2 列、第 %3 行，%4").arg(screen.name).arg(screen.column).arg(screen.row)
                       .arg(m_layout.connectedNames.contains(screen.name) ? tr("在线") : tr("离线"));
    setAccessibleName(tr("屏幕布局，共 %1 台电脑。%2").arg(m_layout.screens.size()).arg(position));
  } else setAccessibleName(layout::isCardinal(m_layout.direction)
                        ? tr("屏幕布局：%1 位于 %2 %3").arg(client, source, directionName(m_layout.direction))
                        : tr("屏幕布局尚未确认"));
  QString description;
  if (m_pending) description = m_feedback.isEmpty() ? tr("正在同步屏幕布局，请稍候。") : m_feedback;
  else if (m_busy) description = tr("正在应用设置，暂时无法调整屏幕布局。");
  else if (!m_canvasFeedback.isEmpty()) description = m_canvasFeedback;
  else if (!m_feedback.isEmpty()) description = m_feedback;
  else if (!m_layout.error.isEmpty()) description = m_layout.error;
  else if (canEdit() && hasTopology()) description = tr("从设备列表选择或点击电脑，用方向键逐格移动；也可拖动到空白格。所有电脑须保持相邻连通。滚轮缩放，拖动空白处平移，适合窗口恢复全图，Esc 取消拖动。");
  else if (canEdit()) description = tr("拖动任一电脑到另一台的上、下、左、右位置。也可聚焦后用方向键调整接收电脑的位置，Esc 取消拖动。确认同步后显示实际布局。");
  else description = tr("当前布局只读，无法在此调整。");
  setAccessibleDescription(description);
  setToolTip(description);
  setCursor(canEdit() ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

QPointF DeviceIllustration::diagramPoint(const QPointF &point) const
{
  const qreal scale = qMin(width() / diagramWidth, height() / diagramHeight);
  if (scale <= 0) return {};
  return (point - QPointF((width() - diagramWidth * scale) / 2, (height() - diagramHeight * scale) / 2)) / scale;
}

Direction DeviceIllustration::targetAt(const QPointF &point) const
{
  if (!rect().contains(point.toPoint())) return Direction::Unknown;
  const auto local = diagramPoint(point);
  // Source movement is the inverse of the receiving computer's position.
  const QPointF anchor = origin;
  for (const auto direction : directions) {
    if (nodeRect(anchor + offset(direction)).adjusted(-12, -8, 12, 8).contains(local))
      return m_dragServer ? layout::opposite(direction) : direction;
  }
  return Direction::Unknown;
}

void DeviceIllustration::cancelDrag()
{
  if (hasTopology() && m_dragging) m_pan = m_panOrigin;
  if (m_pressed || m_dragging || m_panning) m_canvasFeedback.clear();
  m_pressed = false;
  m_dragging = false;
  m_panning = false;
  m_dropDirection = Direction::Unknown;
  setCursor(canEdit() ? Qt::OpenHandCursor : Qt::ArrowCursor);
  update();
}

void DeviceIllustration::requestDirection(Direction direction)
{
  if (canEdit() && layout::isCardinal(direction) && direction != m_layout.direction)
    Q_EMIT screenLayoutRequested(direction);
}

void DeviceIllustration::mousePressEvent(QMouseEvent *event)
{
  if (hasTopology()) {
    if (event->button() != Qt::LeftButton && event->button() != Qt::MiddleButton) return QWidget::mousePressEvent(event);
    const int index = screenAt(event->position());
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::LeftButton && index >= 0) {
      selectScreen(m_layout.screens[index].name);
      if (!canEdit()) return;
      m_pressed = true; m_pressPosition = event->position(); m_pointer = event->position(); m_panOrigin = m_pan;
      m_targetColumn = m_layout.screens[index].column; m_targetRow = m_layout.screens[index].row;
    } else {
      m_panning = true; m_panStart = event->position(); m_panOrigin = m_pan;
      setCursor(Qt::ClosedHandCursor);
    }
    event->accept(); return;
  }
  if (event->button() != Qt::LeftButton || !canEdit()) return QWidget::mousePressEvent(event);
  const auto point = diagramPoint(event->position());
  const bool source = nodeRect(origin).contains(point);
  const bool client = nodeRect(origin + offset(m_layout.direction)).contains(point);
  if (!source && !client) return QWidget::mousePressEvent(event);
  setFocus(Qt::MouseFocusReason);
  m_pressed = true;
  m_dragServer = source;
  m_pressPosition = event->position();
  m_pointer = point;
  event->accept();
}

void DeviceIllustration::mouseMoveEvent(QMouseEvent *event)
{
  if (hasTopology()) {
    if (m_panning) { m_pan = m_panOrigin + event->position() - m_panStart; update(); event->accept(); return; }
    if (!m_pressed) return QWidget::mouseMoveEvent(event);
    if (!canEdit()) { cancelDrag(); return; }
    if (!m_dragging && (event->position() - m_pressPosition).manhattanLength() < QApplication::startDragDistance()) return;
    m_dragging = true; m_pointer = event->position(); previewMove(event->position());
    setCursor(Qt::ClosedHandCursor); update(); event->accept(); return;
  }
  if (!m_pressed) return QWidget::mouseMoveEvent(event);
  if (!canEdit()) { cancelDrag(); return; }
  if (!m_dragging && (event->position() - m_pressPosition).manhattanLength() < QApplication::startDragDistance()) return;
  m_dragging = true;
  m_pointer = diagramPoint(event->position());
  m_dropDirection = targetAt(event->position());
  setCursor(Qt::ClosedHandCursor);
  update();
  event->accept();
}

void DeviceIllustration::mouseReleaseEvent(QMouseEvent *event)
{
  if (hasTopology()) {
    if (m_panning) { cancelDrag(); event->accept(); return; }
    if (event->button() != Qt::LeftButton || !m_pressed) return QWidget::mouseReleaseEvent(event);
    const bool commit = m_dragging && rect().adjusted(0, 40, 0, -24).contains(event->position().toPoint());
    if (commit && event->position() != m_pointer) previewMove(event->position());
    const int column = m_targetColumn, row = m_targetRow;
    cancelDrag(); if (commit) requestMove(column, row);
    refreshAccessibility(); event->accept(); return;
  }
  if (event->button() != Qt::LeftButton || !m_pressed) return QWidget::mouseReleaseEvent(event);
  const Direction requested = m_dragging ? targetAt(event->position()) : Direction::Unknown;
  cancelDrag();
  requestDirection(requested);
  event->accept();
}

void DeviceIllustration::keyPressEvent(QKeyEvent *event)
{
  if (event->key() == Qt::Key_Escape && (m_pressed || m_panning)) { cancelDrag(); event->accept(); return; }
  if (hasTopology() && event->modifiers() == Qt::NoModifier) {
    if (event->key() == Qt::Key_0 || event->key() == Qt::Key_F) { resetView(); event->accept(); return; }
    if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal) { m_zoomIn->click(); event->accept(); return; }
    if (event->key() == Qt::Key_Minus) { m_zoomOut->click(); event->accept(); return; }
    if (event->key() == Qt::Key_Space) {
      const int next = (m_screenPicker->currentIndex() + 1) % m_screenPicker->count();
      m_screenPicker->setCurrentIndex(next); event->accept(); return;
    }
    if (canEdit()) {
      int dx = 0, dy = 0;
      switch (event->key()) { case Qt::Key_Left: dx = -1; break; case Qt::Key_Right: dx = 1; break;
      case Qt::Key_Up: dy = -1; break; case Qt::Key_Down: dy = 1; break; default: break; }
      if (dx || dy) {
        if (m_layout.screens.size() == 2) {
          for (const auto &other : m_layout.screens)
            if (other.name != m_selectedScreen) { requestMove(other.column + dx, other.row + dy); break; }
        } else {
          for (const auto &screen : m_layout.screens)
            if (screen.name == m_selectedScreen) { requestMove(screen.column + dx, screen.row + dy); break; }
        }
        event->accept(); return;
      }
    }
    return QWidget::keyPressEvent(event);
  }
  if (!canEdit() || event->modifiers() != Qt::NoModifier) return QWidget::keyPressEvent(event);
  Direction requested = Direction::Unknown;
  switch (event->key()) {
  case Qt::Key_Left: requested = Direction::Left; break;
  case Qt::Key_Right: requested = Direction::Right; break;
  case Qt::Key_Up: requested = Direction::Above; break;
  case Qt::Key_Down: requested = Direction::Below; break;
  default: return QWidget::keyPressEvent(event);
  }
  cancelDrag();
  requestDirection(requested);
  event->accept();
}

void DeviceIllustration::focusInEvent(QFocusEvent *event) { QWidget::focusInEvent(event); update(); }
void DeviceIllustration::focusOutEvent(QFocusEvent *event) { cancelDrag(); QWidget::focusOutEvent(event); }
void DeviceIllustration::changeEvent(QEvent *event)
{
  if (event->type() == QEvent::EnabledChange) { if (!isEnabled()) cancelDrag(); refreshAccessibility(); }
  QWidget::changeEvent(event);
}

void DeviceIllustration::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  if (hasTopology()) { paintTopology(p); return; }
  const qreal scale = qMin(width() / diagramWidth, height() / diagramHeight);
  p.translate((width() - diagramWidth * scale) / 2, (height() - diagramHeight * scale) / 2);
  p.scale(scale, scale);
  const bool dark = palette().color(QPalette::Window).lightness() < 128;
  const QColor ink = palette().color(QPalette::WindowText);
  const QColor secondary = palette().color(QPalette::PlaceholderText);
  const QColor hardware = dark ? QColor("#a4a7b0") : QColor("#606570");
  const QColor panel = dark ? QColor("#282c34") : QColor("#f4f6f9");
  const QColor accent = personalAccentColor();
  const QColor sourceColor = m_sourceOS == shortcuts::DeviceOS::Linux ? QColor("#e95420") : accent;
  const bool known = layout::isCardinal(m_layout.direction);
  const bool editable = canEdit();
  QFont text = font();

  const auto drawDevice = [&](const QPointF &center, bool source, qreal opacity = 1.0) {
    p.save();
    p.setOpacity(opacity);
    p.translate(center);
    const QRectF display(-60, -41, 120, 44);
    p.setBrush(source ? panel : (dark ? QColor("#243443") : QColor("#e4effb")));
    p.setPen(QPen(hardware, 2.2));
    p.drawRoundedRect(display, 4, 4);
    p.setPen(Qt::NoPen);
    p.setBrush(hardware);
    if (source) {
      p.drawRoundedRect(QRectF(-6, 4, 12, 4), 1, 1);
      p.drawRoundedRect(QRectF(-24, 8, 48, 3), 1, 1);
    } else {
      p.drawRoundedRect(QRectF(-69, 4, 138, 5), 2, 2);
    }
    p.setPen(source ? sourceColor : accent);
    text.setPointSize(10); text.setWeight(QFont::DemiBold); p.setFont(text);
    const auto local = shortcuts::localOS() == shortcuts::DeviceOS::MacOS ? tr("此 Mac") : tr("此电脑");
    p.drawText(display.adjusted(5, 2, -5, -2), Qt::AlignCenter,
               source ? displayOperatingSystem(m_sourceOS) : (m_serverMode ? tr("接收电脑") : local));
    const auto name = source
        ? (m_layout.serverName.isEmpty() ? (m_serverMode ? m_macName : m_host) : m_layout.serverName)
        : (m_layout.clientName.isEmpty() ? m_macName : m_layout.clientName);
    text.setPointSize(10); text.setWeight(QFont::DemiBold); p.setFont(text); p.setPen(ink);
    p.drawText(QRectF(-76, 11, 152, 18), Qt::AlignCenter,
               p.fontMetrics().elidedText(name.isEmpty() ? (source ? tr("键鼠来源") : tr("接收电脑")) : name,
                                         Qt::ElideMiddle, 148));
    text.setPointSize(8); text.setWeight(QFont::Normal); p.setFont(text); p.setPen(secondary);
    p.drawText(QRectF(-76, 28, 152, 15), Qt::AlignCenter, source ? tr("键鼠来源") : tr("接收端"));
    p.restore();
  };

  if (!known) {
    drawDevice(QPointF(300, 117), true, 0.65);
    text.setPointSize(11); text.setWeight(QFont::Normal); p.setFont(text); p.setPen(secondary);
    p.drawText(QRectF(80, 189, 440, 30), Qt::AlignCenter, tr("尚未读取实际屏幕布局"));
    return;
  }

  // During a source drag, center the receiving computer as the stationary
  // reference. Translation preserves the confirmed relationship; the ghost
  // previews where the source will be placed relative to that receiver.
  const bool sourceDrag = m_dragging && m_dragServer;
  const QPointF source = sourceDrag ? origin - offset(m_layout.direction) : origin;
  const QPointF client = sourceDrag ? origin : origin + offset(m_layout.direction);
  // The line joins the two confirmed positions, separate from the drag ghost.
  p.setPen(QPen(m_connected ? accent : secondary, 1.7, m_connected ? Qt::SolidLine : Qt::DashLine));
  const auto delta = client - source;
  const bool horizontal = delta.x() != 0;
  const QPointF start = source + QPointF(horizontal ? std::copysign(78, delta.x()) : 0,
                                         horizontal ? 0 : std::copysign(43, delta.y()));
  const QPointF end = client - QPointF(horizontal ? std::copysign(78, delta.x()) : 0,
                                       horizontal ? 0 : std::copysign(43, delta.y()));
  p.drawLine(start, end);

  if (editable && (hasFocus() || m_dragging)) {
    const QPointF anchor = origin;
    for (const auto direction : directions) {
      const auto target = anchor + offset(direction);
      if (!QRectF(0, 0, diagramWidth, diagramHeight).contains(nodeRect(target))) continue;
      const auto requested = m_dragging && m_dragServer ? layout::opposite(direction) : direction;
      const bool highlighted = m_dragging && requested == m_dropDirection;
      p.setPen(QPen(highlighted ? accent : (dark ? QColor("#555b65") : QColor("#c9ced7")),
                    highlighted ? 2 : 1, Qt::DashLine));
      auto fill = accent; fill.setAlpha(highlighted ? 35 : 8); p.setBrush(fill);
      p.drawRoundedRect(nodeRect(target), 9, 9);
      if (target == source || target == client) continue;
      text.setPointSize(10); text.setWeight(QFont::Normal); p.setFont(text); p.setPen(highlighted ? accent : secondary);
      p.drawText(nodeRect(target), Qt::AlignCenter, directionName(direction));
    }
  }
  drawDevice(source, true, sourceDrag ? 0.35 : 1);
  drawDevice(client, false, m_dragging && !m_dragServer ? 0.35 : 1);
  if (m_dragging) {
    drawDevice(m_pointer, m_dragServer, 0.8);
  }
  if (m_pending) {
    text.setPointSize(9); text.setWeight(QFont::Normal); p.setFont(text); p.setPen(accent);
    const QRectF badge(horizontal ? QRectF(215, 190, 170, 23) : QRectF(405, 129, 180, 23));
    p.drawText(badge, Qt::AlignCenter, tr("正在同步…"));
  }
  if (hasFocus()) {
    p.setBrush(Qt::NoBrush); auto focus = accent; focus.setAlpha(160);
    p.setPen(QPen(focus, 1.4));
    p.drawRoundedRect(QRectF(2, 2, diagramWidth - 4, diagramHeight - 4), 11, 11);
  }
}

bool DeviceIllustration::hasTopology() const { return !m_layout.screens.isEmpty(); }

QTransform DeviceIllustration::canvasTransform() const
{
  if (!hasTopology()) return {};
  int minColumn = m_layout.screens.first().column, maxColumn = minColumn;
  int minRow = m_layout.screens.first().row, maxRow = minRow;
  for (const auto &screen : m_layout.screens) {
    minColumn = qMin(minColumn, screen.column); maxColumn = qMax(maxColumn, screen.column);
    minRow = qMin(minRow, screen.row); maxRow = qMax(maxRow, screen.row);
  }
  const QRectF content(10, 43, qMax(1, width() - 20), qMax(1, height() - 68));
  const QSizeF extent((maxColumn - minColumn) * 150 + 156, (maxRow - minRow) * 96 + 96);
  const qreal scale = qMin(1.25, qMin(content.width() / extent.width(), content.height() / extent.height())) * m_zoom;
  QTransform transform;
  transform.translate(content.center().x() + m_pan.x(), content.center().y() + m_pan.y());
  transform.scale(scale, scale);
  transform.translate(-(minColumn + maxColumn) * 75.0, -(minRow + maxRow) * 48.0);
  return transform;
}

QRectF DeviceIllustration::screenGeometry(const QString &name) const
{
  for (const auto &screen : m_layout.screens)
    if (screen.name == name)
      return canvasTransform().mapRect(QRectF(screen.column * 150 - 66, screen.row * 96 - 36, 132, 72));
  return {};
}

int DeviceIllustration::screenAt(const QPointF &point) const
{
  if (point.y() < 42 || point.y() > height() - 24) return -1;
  const auto local = canvasTransform().inverted().map(point);
  for (int i = 0; i < m_layout.screens.size(); ++i) {
    const auto &screen = m_layout.screens[i];
    if (QRectF(screen.column * 150 - 66, screen.row * 96 - 36, 132, 72).contains(local)) return i;
  }
  return -1;
}

void DeviceIllustration::updateCanvasControls()
{
  if (!m_screenPicker) return;
  const bool visible = hasTopology();
  const std::array<QWidget *, 4> controls{m_screenPicker, m_fit, m_zoomIn, m_zoomOut};
  for (auto *widget : controls) widget->setVisible(visible);
  if (!visible) return;
  const QSignalBlocker blocker(m_screenPicker);
  QStringList names;
  for (const auto &screen : m_layout.screens) names.append(screen.name);
  if (!names.contains(m_selectedScreen))
    m_selectedScreen = names.contains(m_layout.localName) ? m_layout.localName : m_layout.serverName;
  if (!names.contains(m_selectedScreen)) m_selectedScreen = names.first();
  QStringList listed;
  for (int i = 0; i < m_screenPicker->count(); ++i) listed.append(m_screenPicker->itemText(i));
  if (listed != names) { m_screenPicker->clear(); m_screenPicker->addItems(names); }
  m_screenPicker->setCurrentText(m_selectedScreen);
  m_screenPicker->setGeometry(10, 4, qMax(110, qMin(180, width() - 160)), 32);
  m_fit->setGeometry(width() - 86, 4, 76, 32);
  m_zoomIn->setGeometry(width() - 122, 4, 32, 32);
  m_zoomOut->setGeometry(width() - 158, 4, 32, 32);
}

void DeviceIllustration::selectScreen(const QString &name)
{
  if (name == m_selectedScreen) return;
  cancelDrag(); m_selectedScreen = name; m_canvasFeedback.clear();
  const QSignalBlocker blocker(m_screenPicker); m_screenPicker->setCurrentText(name);
  refreshAccessibility(); update();
}

void DeviceIllustration::previewMove(const QPointF &point)
{
  const auto local = canvasTransform().inverted().map(point);
  m_targetColumn = qRound(local.x() / 150); m_targetRow = qRound(local.y() / 96);
  // Keep the snapped cell in view while the pointer approaches an outer edge.
  const QRectF target = canvasTransform().mapRect(QRectF(m_targetColumn * 150 - 66, m_targetRow * 96 - 36, 132, 72));
  const QRectF area(10, 45, width() - 20, height() - 72);
  QPointF adjustment;
  if (target.left() < area.left()) adjustment.setX(area.left() - target.left());
  else if (target.right() > area.right()) adjustment.setX(area.right() - target.right());
  if (target.top() < area.top()) adjustment.setY(area.top() - target.top());
  else if (target.bottom() > area.bottom()) adjustment.setY(area.bottom() - target.bottom());
  if (rect().adjusted(0, 40, 0, -24).contains(point.toPoint())) m_pan += adjustment;
  QString error;
  const layout::Move move{m_selectedScreen, m_targetColumn, m_targetRow, m_layout.revision};
  m_previewValid = layout::movedSnapshot(m_layout, move, &error).has_value();
  m_canvasFeedback = m_previewValid ? tr("松开后同步 %1 的位置").arg(m_selectedScreen) : error;
  refreshAccessibility();
}

void DeviceIllustration::requestMove(int column, int row)
{
  if (!canEdit()) return;
  for (const auto &screen : m_layout.screens)
    if (screen.name == m_selectedScreen && screen.column == column && screen.row == row) {
      m_canvasFeedback.clear(); refreshAccessibility(); update(); return;
    }
  QString error;
  const layout::Move move{m_selectedScreen, column, row, m_layout.revision};
  if (!layout::movedSnapshot(m_layout, move, &error)) {
    m_canvasFeedback = error; refreshAccessibility(); update(); return;
  }
  m_canvasFeedback.clear(); refreshAccessibility(); update();
  Q_EMIT screenMoveRequested(move);
}

void DeviceIllustration::resetView() { m_zoom = 1; m_pan = {}; update(); }

void DeviceIllustration::resizeEvent(QResizeEvent *event)
{
  QWidget::resizeEvent(event); updateCanvasControls();
}

void DeviceIllustration::wheelEvent(QWheelEvent *event)
{
  if (!hasTopology()) return QWidget::wheelEvent(event);
  const auto world = canvasTransform().inverted().map(event->position());
  m_zoom = qBound(0.4, m_zoom * (event->angleDelta().y() > 0 ? 1.15 : 1 / 1.15), 4.0);
  m_pan += event->position() - canvasTransform().map(world);
  update(); event->accept();
}

void DeviceIllustration::paintTopology(QPainter &p)
{
  const bool dark = palette().color(QPalette::Window).lightness() < 128;
  const QColor ink = palette().color(QPalette::WindowText);
  const QColor secondary = palette().color(QPalette::PlaceholderText);
  const QColor accent = personalAccentColor();
  const QColor danger = dark ? QColor("#ff9c91") : QColor("#bf3026");
  const auto transform = canvasTransform();
  const qreal scale = transform.m11();
  const QRectF canvas(4, 41, width() - 8, height() - 65);
  p.save(); p.setClipRect(canvas); p.setTransform(transform);
  const auto visible = transform.inverted().mapRect(canvas);
  QColor grid = secondary; grid.setAlpha(dark ? 48 : 40);
  p.setPen(QPen(grid, 2 / scale));
  for (int row = qMax(-64, int(std::floor(visible.top() / 96))); row <= qMin(64, int(std::ceil(visible.bottom() / 96))); ++row)
    for (int column = qMax(-64, int(std::floor(visible.left() / 150))); column <= qMin(64, int(std::ceil(visible.right() / 150))); ++column)
      p.drawPoint(QPointF(column * 150, row * 96));
  p.setPen(QPen(dark ? QColor("#505762") : QColor("#b7c0ce"), 1.4 / scale));
  for (int i = 0; i < m_layout.screens.size(); ++i)
    for (int j = i + 1; j < m_layout.screens.size(); ++j) {
      const auto &a = m_layout.screens[i], &b = m_layout.screens[j];
      if (qAbs(a.column - b.column) + qAbs(a.row - b.row) == 1)
        p.drawLine(QPointF(a.column * 150, a.row * 96), QPointF(b.column * 150, b.row * 96));
    }
  const auto drawNode = [&](const layout::Screen &screen, const QPointF &center, bool ghost) {
    p.save(); p.translate(center);
    const bool selected = screen.name == m_selectedScreen;
    const bool source = screen.name == m_layout.serverName;
    const bool local = screen.name == m_layout.localName;
    const bool online = m_layout.connectedNames.contains(screen.name);
    const QColor outline = ghost ? (m_previewValid ? accent : danger) : (selected ? accent : (dark ? QColor("#555c67") : QColor("#c3cad4")));
    if (m_dragging && selected && !ghost) p.setOpacity(0.3);
    p.setPen(QPen(outline, (ghost || selected ? 2 : 1) / scale, ghost ? Qt::DashLine : Qt::SolidLine));
    p.setBrush(dark ? QColor("#292e37") : QColor("#ffffff"));
    p.drawRoundedRect(QRectF(-66, -36, 132, 72), 7, 7);
    if (source) { auto tint = accent; tint.setAlpha(18); p.setBrush(tint); p.setPen(Qt::NoPen); p.drawRoundedRect(QRectF(-65, -35, 130, 70), 6, 6); }
    p.setPen(QPen(ghost ? outline : secondary, 1.3 / scale)); p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(-55, -25, 17, 12), 1, 1); p.drawLine(QPointF(-47, -13), QPointF(-47, -9));
    p.drawLine(QPointF(-52, -9), QPointF(-42, -9));
    QFont text = font(); text.setPixelSize(qMax(8, qRound(12 / scale))); text.setWeight(QFont::DemiBold); p.setFont(text); p.setPen(ink);
    p.drawText(QRectF(-31, -29, 88, 25), Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(screen.name, Qt::ElideMiddle, 87));
    text.setPixelSize(qMax(7, qRound(10 / scale))); text.setWeight(QFont::Normal); p.setFont(text); p.setPen(secondary);
    QString role = source ? tr("键鼠来源") : tr("接收端");
    if (local) role += tr(" · 本机");
    p.drawText(QRectF(-55, -2, 112, 18), Qt::AlignVCenter, p.fontMetrics().elidedText(role, Qt::ElideRight, 112));
    p.setPen(Qt::NoPen); p.setBrush(online ? QColor("#29a74b") : (dark ? QColor("#7d8590") : QColor("#9aa1ab")));
    p.drawEllipse(QPointF(-52, 23), 2.5 / scale, 2.5 / scale);
    p.setPen(secondary);
    p.drawText(QRectF(-43, 14, 98, 17), Qt::AlignVCenter, online ? tr("在线") : tr("离线"));
    p.restore();
  };
  for (const auto &screen : m_layout.screens)
    drawNode(screen, {qreal(screen.column * 150), qreal(screen.row * 96)}, false);
  if (m_dragging) {
    for (const auto &screen : m_layout.screens)
      if (screen.name == m_selectedScreen) drawNode(screen, {qreal(m_targetColumn * 150), qreal(m_targetRow * 96)}, true);
  }
  p.restore();
  QFont caption = font(); caption.setPointSize(9); p.setFont(caption);
  p.setPen(m_canvasFeedback.isEmpty() || (m_dragging && m_previewValid) ? secondary : danger);
  const QString footer = m_canvasFeedback.isEmpty()
      ? tr("%1 台电脑 · 滚轮缩放 · 拖动空白处平移 · 空格切换设备").arg(m_layout.screens.size()) : m_canvasFeedback;
  p.drawText(QRectF(12, height() - 22, width() - 24, 20), Qt::AlignCenter,
             p.fontMetrics().elidedText(footer, Qt::ElideRight, width() - 24));
  if (hasFocus()) {
    auto focus = accent; focus.setAlpha(160); p.setPen(QPen(focus, 1.2)); p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(canvas.adjusted(1, 1, -1, -1), 7, 7);
  }
}
} // namespace deskflow::gui::personal
