/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#pragma once

#include "common/ScreenLayout.h"
#include "common/ShortcutRules.h"
#include <QWidget>
#include <QTransform>

class QPainter;
class QComboBox;
class QToolButton;

namespace deskflow::gui::personal {
class DeviceIllustration : public QWidget
{
  Q_OBJECT
public:
  explicit DeviceIllustration(QWidget *parent = nullptr);
  void setConnection(bool connected, const QString &host, const QString &macName);
  void setDeviceContext(shortcuts::DeviceOS sourceOS, bool server);
  void setScreenLayout(const layout::Snapshot &snapshot, bool pending = false, const QString &feedback = {});
  void setConfigurationBusy(bool busy);
  const layout::Snapshot &screenLayout() const { return m_layout; }
  QSize sizeHint() const override;
  QRectF screenGeometry(const QString &name) const;

Q_SIGNALS:
  void screenLayoutRequested(deskflow::layout::Direction direction);
  void screenMoveRequested(const deskflow::layout::Move &move);

protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void focusInEvent(QFocusEvent *event) override;
  void focusOutEvent(QFocusEvent *event) override;
  void changeEvent(QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;

private:
  bool canEdit() const;
  bool hasTopology() const;
  QTransform canvasTransform() const;
  int screenAt(const QPointF &point) const;
  void paintTopology(QPainter &painter);
  void updateCanvasControls();
  void selectScreen(const QString &name);
  void previewMove(const QPointF &point);
  void requestMove(int column, int row);
  void resetView();
  void cancelDrag();
  void refreshAccessibility();
  QPointF diagramPoint(const QPointF &point) const;
  layout::Direction targetAt(const QPointF &point) const;
  void requestDirection(layout::Direction direction);
  QComboBox *m_screenPicker = nullptr;
  QToolButton *m_fit = nullptr;
  QToolButton *m_zoomIn = nullptr;
  QToolButton *m_zoomOut = nullptr;
  QString m_selectedScreen;
  QString m_canvasFeedback;
  bool m_previewValid = false;
  int m_targetColumn = 0;
  int m_targetRow = 0;
  qreal m_zoom = 1;
  QPointF m_pan;
  bool m_panning = false;
  QPointF m_panStart;
  QPointF m_panOrigin;
  bool m_connected = false;
  QString m_host;
  QString m_macName;
  shortcuts::DeviceOS m_sourceOS = shortcuts::DeviceOS::Unknown;
  bool m_serverMode = false;
  layout::Snapshot m_layout;
  bool m_pending = false;
  bool m_busy = false;
  QString m_feedback;
  bool m_pressed = false;
  bool m_dragging = false;
  bool m_dragServer = false;
  QPointF m_pressPosition;
  QPointF m_pointer;
  layout::Direction m_dropDirection = layout::Direction::Unknown;
};
} // namespace deskflow::gui::personal
