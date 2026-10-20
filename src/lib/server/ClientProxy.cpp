/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy.h"

#include "base/Log.h"
#include "deskflow/ProtocolUtil.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ScreenLayoutProtocol.h"
#include "io/IStream.h"

//
// ClientProxy
//

ClientProxy::ClientProxy(const std::string &name, deskflow::IStream *stream) : BaseClientProxy(name), m_stream(stream)
{
  // do nothing
}

ClientProxy::~ClientProxy()
{
  delete m_stream;
}

void ClientProxy::close(const char *msg) const
{
  LOG_VERBOSE("send close \"%s\" to \"%s\"", msg, getName().c_str());
  ProtocolUtil::writef(getStream(), msg);

  // force the close to be sent before we return
  getStream()->flush();
}

deskflow::IStream *ClientProxy::getStream() const
{
  return m_stream;
}

void *ClientProxy::getEventTarget() const
{
  return static_cast<IComputer *>(const_cast<ClientProxy *>(this));
}

void ClientProxy::sendScreenLayout(const deskflow::layout::Snapshot &snapshot, quint32 requestId, bool success)
{
  if (!m_layoutSubscribed || !m_layoutAdvertised)
    return;
  if (m_layoutVersion == 2) {
    auto local = snapshot;
    // The recipient's canonical name highlights the correct node even when it
    // connected with an alias. Server publishes canonical connected names.
    if (!local.screens.isEmpty()) {
      // The server stamps localName before this boundary.
      if (local.localName.isEmpty()) local.localName = QString::fromStdString(getName());
    }
    deskflow::layout::writeWireObject(getStream(), kMsgDScreenLayoutV2,
        {{"id", static_cast<double>(requestId)}, {"success", success}, {"snapshot", deskflow::layout::toJson(local)}});
    return;
  }
  const auto server = snapshot.serverName.left(deskflow::layout::kMaxNameLength).toStdString();
  const auto client = QString::fromStdString(getName()).left(deskflow::layout::kMaxNameLength).toStdString();
  const auto error = snapshot.error.left(deskflow::layout::kMaxErrorLength).toStdString();
  ProtocolUtil::writef(getStream(), kMsgDScreenLayout, static_cast<uint32_t>(snapshot.direction),
                       snapshot.editable ? 1 : 0, requestId, success ? 1 : 0, &server, &client, &error);
}
