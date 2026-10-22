/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ComputerSetupView.h"

#include "ComputerSetupModel.h"
#include "common/Settings.h"
#include "dialogs/ComputerSettingsDialog.h"
#include "personal/PersonalStyle.h"

#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QHeaderView>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QStyledItemDelegate>

namespace {
class DeviceCellDelegate : public QStyledItemDelegate
{
public:
  explicit DeviceCellDelegate(QObject *parent) : QStyledItemDelegate(parent)
  {
  }
  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
  {
    auto *model = qobject_cast<const ComputerSetupModel *>(index.model());
    if (!model)
      return;
    const auto *item = model->computerAt(index);
    if (!item)
      return;
    const auto &computer = *item;
    const bool dark = option.palette.color(QPalette::Window).lightness() < 128;
    const auto accent = deskflow::gui::personal::personalAccentColor();
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    const QRectF rect = QRectF(option.rect).adjusted(5, 5, -5, -5);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(
        selected ? accent : QColor(dark ? "#4a4b53" : "#dcdde4"), selected ? 2 : 1,
        computer.isNull() ? Qt::DashLine : Qt::SolidLine
    ));
    painter->setBrush(computer.isNull() ? Qt::transparent : QColor(dark ? "#292a2f" : "#ffffff"));
    painter->drawRoundedRect(rect, 8, 8);
    if (computer.isNull()) {
      painter->setPen(QColor(dark ? "#777780" : "#a0a0ac"));
      const auto center = rect.center();
      painter->drawLine(center + QPointF(-6, 0), center + QPointF(6, 0));
      painter->drawLine(center + QPointF(0, -6), center + QPointF(0, 6));
    } else {
      const auto color = selected ? accent : option.palette.color(QPalette::WindowText);
      auto icon = deskflow::gui::personal::personalIcon(QStringLiteral("devices"), color);
      icon.paint(painter, QRect(rect.center().x() - 18, rect.top() + 12, 36, 36));
      auto font = option.font;
      font.setPointSize(11);
      font.setWeight(QFont::DemiBold);
      painter->setFont(font);
      painter->setPen(option.palette.color(QPalette::WindowText));
      const auto label = painter->fontMetrics().elidedText(computer.name(), Qt::ElideMiddle, rect.width() - 12);
      painter->drawText(rect.adjusted(6, 50, -6, -20), Qt::AlignHCenter | Qt::AlignTop, label);
      font.setPointSize(9);
      font.setWeight(QFont::Normal);
      painter->setFont(font);
      painter->setPen(QColor(dark ? "#aaaab4" : "#686873"));
      painter->drawText(
          rect.adjusted(6, 0, -6, -6), Qt::AlignHCenter | Qt::AlignBottom,
          computer.isServer() ? QObject::tr("键鼠来源") : QObject::tr("接收设备")
      );
    }
    painter->restore();
  }
};
} // namespace

ComputerSetupView::ComputerSetupView(QWidget *parent) : QTableView(parent)
{
  setDropIndicatorShown(true);
  setDragDropMode(DragDrop);
  setSelectionMode(SingleSelection);
  setShowGrid(false);
  setItemDelegate(new DeviceCellDelegate(this));

  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  setIconSize(QSize(96, 96));
  horizontalHeader()->hide();
  verticalHeader()->hide();
}

void ComputerSetupView::setModel(QAbstractItemModel *model)
{
  QTableView::setModel(model);
  setTableSize();
}

ComputerSetupModel *ComputerSetupView::model() const
{
  return qobject_cast<ComputerSetupModel *>(QTableView::model());
}

void ComputerSetupView::showComputerConfig(int col, int row)
{
  ComputerSettingsDialog dlg(this, &model()->computer(col, row), &model()->m_computers);
  dlg.exec();
  Q_EMIT model()->computersChanged();
}

void ComputerSetupView::setTableSize()
{
  if (!model() || model()->columnCount() == 0 || model()->rowCount() == 0)
    return;
  for (int i = 0; i < model()->columnCount(); i++)
    setColumnWidth(i, viewport()->width() / model()->columnCount());

  for (int i = 0; i < model()->rowCount(); i++)
    setRowHeight(i, viewport()->height() / model()->rowCount());
}

void ComputerSetupView::resizeEvent(QResizeEvent *event)
{
  setTableSize();
  event->ignore();
}

void ComputerSetupView::mouseDoubleClickEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton && model() && Settings::isWritable()) {
    const auto index = indexAt(event->position().toPoint());
    if (index.isValid() && !model()->computer(index).isNull())
      showComputerConfig(index.column(), index.row());
  } else
    event->ignore();
}

void ComputerSetupView::dragEnterEvent(QDragEnterEvent *event)
{
  // we accept anything that enters us by a drag as long as the
  // mime type is okay. anything else is dealt with in dragMoveEvent()
  if (event->mimeData()->hasFormat(ComputerSetupModel::mimeType()))
    event->accept();
  else
    event->ignore();
}

void ComputerSetupView::dragMoveEvent(QDragMoveEvent *event)
{
  if (event->mimeData()->hasFormat(ComputerSetupModel::mimeType())) {
    // where does the event come from? myself or someone else?
    if (event->source() == this) {
      // myself is ok, but then it must be a move action, never a copy
      event->setDropAction(Qt::MoveAction);
      event->accept();
    } else {
      const auto &point = event->position().toPoint();
      int col = columnAt(point.x());
      int row = rowAt(point.y());
      if (col < 0 || row < 0 || col >= model()->columnCount() || row >= model()->rowCount()) {
        event->ignore();
        return;
      }

      // a drop from outside is not allowed if there's a computer already there.
      if (!model()->computer(col, row).isNull())
        event->ignore();
      else {
        event->acceptProposedAction();
      }
    }
  } else
    event->ignore();
}

// this is reimplemented from QAbstractItemView::startDrag()
void ComputerSetupView::startDrag(Qt::DropActions)
{
  QModelIndexList indexes = selectedIndexes();

  if (indexes.count() != 1)
    return;

  QMimeData *pData = model()->mimeData(indexes);
  if (pData == nullptr)
    return;

  const QPixmap &pixmap = model()->computer(indexes[0]).pixmap();
  auto *pDrag = new QDrag(this);
  pDrag->setPixmap(pixmap);
  pDrag->setMimeData(pData);
  pDrag->setHotSpot(QPoint(iconSize().width() / 2, iconSize().height() / 2));

  if (pDrag->exec(Qt::MoveAction, Qt::MoveAction) == Qt::MoveAction) {
    selectionModel()->clear();

    // make sure to only delete the drag source if computerss weren't swapped
    // see ComputerSetupModel::dropMimeData
    if (!model()->computer(indexes[0]).swapped())
      model()->computer(indexes[0]) = Computer();
    else
      model()->computer(indexes[0]).setSwapped(false);

    Q_EMIT model()->computersChanged();
  }
}

void ComputerSetupView::initViewItemOption(QStyleOptionViewItem *option) const
{
  QTableView::initViewItemOption(option);

  option->decorationSize = QSize(96, 96);
  option->showDecorationSelected = true;
  option->decorationPosition = QStyleOptionViewItem::Top;
  option->decorationAlignment = Qt::AlignHCenter | Qt::AlignVCenter;
  option->displayAlignment = Qt::AlignTop | Qt::AlignHCenter;
  option->textElideMode = Qt::ElideMiddle;
}
