/*
    Copyright (c) 2011-2013 Andrey Sibiryov <me@kobology.ru>
    Copyright (c) 2011-2013 Other contributors as noted in the AUTHORS file.

    This file is part of Cocaine.

    Cocaine is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    Cocaine is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "cocaine/detail/actor.hpp"

#include "cocaine/api/stream.hpp"

#include "cocaine/asio/acceptor.hpp"
#include "cocaine/asio/connector.hpp"
#include "cocaine/asio/reactor.hpp"
#include "cocaine/asio/socket.hpp"
#include "cocaine/asio/tcp.hpp"

#include "cocaine/dispatch.hpp"
#include "cocaine/memory.hpp"
#include "cocaine/messages.hpp"

#include "cocaine/rpc/channel.hpp"

#include "cocaine/traits/literal.hpp"

#if defined(__linux__)
    #include <sys/prctl.h>
#endif

using namespace cocaine;
using namespace cocaine::io;

using namespace std::placeholders;

namespace {

struct upstream_t:
    public api::stream_t
{
    upstream_t(const std::shared_ptr<channel<io::socket<tcp>>>& channel, uint64_t tag):
        m_channel(channel),
        m_tag(tag)
    { }

    virtual
    void
    write(const char* chunk, size_t size) {
        const auto ptr = m_channel.lock();

        if(ptr) {
            ptr->wr->write<rpc::chunk>(m_tag, literal { chunk, size });
        }
    }

    virtual
    void
    error(int code, const std::string& reason) {
        const auto ptr = m_channel.lock();

        if(ptr) {
            ptr->wr->write<rpc::error>(m_tag, code, reason);
        }
    }

    virtual
    void
    close() {
        const auto ptr = m_channel.lock();

        if(ptr) {
            m_channel.reset();

            // Due to reset() above, further messages would be discarded.
            ptr->wr->write<rpc::choke>(m_tag);
        }
    }

private:
    // Non-const because this pointer will be reset in close().
    std::weak_ptr<channel<io::socket<tcp>>> m_channel;

    const uint64_t m_tag;
};

}

actor_t::actor_t(std::shared_ptr<reactor_t> reactor, std::unique_ptr<dispatch_t>&& dispatch):
    m_reactor(reactor),
    m_dispatch(std::move(dispatch))
{ }

actor_t::~actor_t() {
    // Empty.
}

namespace {

struct named_runnable {
    void
    operator()() const {
#if defined(__linux__)
        if(name.size() < 16) {
            ::prctl(PR_SET_NAME, name.c_str());
        } else {
            ::prctl(PR_SET_NAME, name.substr(0, 16).data());
        }
#endif

        reactor->run();
    }

    const std::string name;
    const std::shared_ptr<reactor_t>& reactor;
};

}

void
actor_t::run(std::vector<tcp::endpoint> endpoints) {
    BOOST_ASSERT(!m_thread);

    for(auto it = endpoints.cbegin(); it != endpoints.cend(); ++it) {
        m_connectors.emplace_back(
            *m_reactor,
            std::make_unique<acceptor<tcp>>(*it)
        );

        m_connectors.back().bind(std::bind(&actor_t::on_connection, this, _1));
    }

    m_thread.reset(new std::thread(named_runnable {
        m_dispatch->name(),
        m_reactor
    }));
}

void
actor_t::terminate() {
    BOOST_ASSERT(m_thread);

    m_reactor->post(std::bind(&reactor_t::stop, m_reactor));

    m_thread->join();
    m_thread.reset();

    m_connectors.clear();
}

auto
actor_t::endpoints() const -> std::vector<tcp::endpoint> {
    BOOST_ASSERT(!m_connectors.empty());

    std::vector<tcp::endpoint> endpoints;

    for(auto it = m_connectors.begin(); it != m_connectors.end(); ++it) {
        endpoints.push_back(it->endpoint());
    }

    return endpoints;
}

auto
actor_t::dispatch() -> dispatch_t& {
    return *m_dispatch;
}

void
actor_t::on_connection(const std::shared_ptr<io::socket<tcp>>& socket_) {
    auto channel_ = std::make_shared<channel<io::socket<tcp>>>(*m_reactor, socket_);

    // Shortcut, disposable.
    const int fd = socket_->fd();

    channel_->rd->bind(
        std::bind(&actor_t::on_message, this, fd, _1),
        std::bind(&actor_t::on_failure, this, fd, _1)
    );

    channel_->wr->bind(
        std::bind(&actor_t::on_failure, this, fd, _1)
    );

    m_channels[fd] = channel_;
}

void
actor_t::on_message(int fd, const message_t& message) {
    auto it = m_channels.find(fd);

    if(it == m_channels.end()) {
        return;
    }

    m_dispatch->invoke(message, std::make_shared<upstream_t>(
        it->second,
        message.band()
    ));
}

void
actor_t::on_failure(int fd, const std::error_code& /* ec */) {
    m_channels.erase(fd);
}
