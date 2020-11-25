#include <net/tcp_server.h>
#include <net/socket_utils.h>

namespace sms
{
    inline static void on_connection(uv_stream_t *handle, int status)
    {
        TcpServer *server = reinterpret_cast<TcpServer *>(handle->data);
        if (server)
        {
            server->OnUvConnection(status);
        }
    }

    inline static void on_close(uv_handle_t *handle)
    {
        handle->data = nullptr;
        delete handle;
    }

    TcpServer::TcpServer()
    {
    }

    TcpServer::~TcpServer()
    {
        Close();
    }

    void TcpServer::Close()
    {
        if (closed_ || !uv_handle_)
        {
            return;
        }

        closed_ = true;

        for (TcpConnection *conn : conns_)
        {
            delete conn;
        }

        uv_close(reinterpret_cast<uv_handle_t *>(uv_handle_), static_cast<uv_close_cb>(on_close));
    }

    int TcpServer::Start(uv_tcp_t *handle, int backlog)
    {
        uv_handle_ = handle;
        if (!uv_handle_)
        {
            return -1;
        }

        uv_handle_->data = this;

        int ret = uv_listen(reinterpret_cast<uv_stream_t *>(uv_handle_),
                            backlog,
                            static_cast<uv_connection_cb>(on_connection));
        if (ret != 0)
        {
            uv_close(reinterpret_cast<uv_handle_t *>(uv_handle_),
                     static_cast<uv_close_cb>(on_close));
            LOG_E << "uv_listen failed [ip:" << local_ip_ << ", port:" << local_port_ << "]: " << uv_strerror(ret);
            return -1;
        }

        if (!set_local_addr())
        {
            uv_close(reinterpret_cast<uv_handle_t *>(uv_handle_), static_cast<uv_close_cb>(on_close));
            LOG_E << "error setting local IP and port";
            return -1;
        }

        return 0;
    }

    void TcpServer::Dump() const
    {
        LOG_I << SMS_LOG_SEPARATOR_CHAR_STD
              << "<TcpServer>" << SMS_LOG_SEPARATOR_CHAR_STD
              << "  localIp     : " << local_ip_ << SMS_LOG_SEPARATOR_CHAR_STD
              << "  localPort   : " << local_port_ << SMS_LOG_SEPARATOR_CHAR_STD
              << "  closed      : " << closed_ << SMS_LOG_SEPARATOR_CHAR_STD
              << "  connections : " << conns_.size() << SMS_LOG_SEPARATOR_CHAR_STD
              << "</TcpServer>" << std::endl;
    }

    size_t TcpServer::OnTcpConnectionPacketReceived(TcpConnection *conn, const uint8_t *data, size_t len)
    {
        return len;
    }

    void TcpServer::OnTcpConnectionClosed(TcpConnection *conn)
    {
        conns_.erase(conn);
        delete conn;
    }

    void TcpServer::OnUvConnection(int status)
    {
        if (closed_)
        {
            return;
        }
        if (status != 0)
        {
            LOG_E << "error while receiving a new TCP connection: " << uv_strerror(status);
            return;
        }

        TcpConnection *conn = new TcpConnection(128 * 1024 * 1024);
        int ret = conn->Setup(this, &local_addr_, local_ip_, local_port_);
        if (ret != 0)
        {
            delete conn;
            return;
        }

        ret = uv_accept(reinterpret_cast<uv_stream_t *>(uv_handle_),
                        reinterpret_cast<uv_stream_t *>(conn->GetUvHandle()));
        if (ret != 0)
        {
            SMS_ABORT("uv_accept failed: %s", uv_strerror(ret));
        }

        ret = conn->Start();
        if (ret != 0)
        {
            delete conn;
            return;
        }

        conns_.insert(conn);
    }

    bool TcpServer::set_local_addr()
    {
        int len = sizeof(local_addr_);

        int ret = uv_tcp_getsockname(uv_handle_, reinterpret_cast<sockaddr *>(&local_addr_), &len);
        if (ret != 0)
        {
            LOG_E << "uv_tcp_getsockname failed: " << uv_strerror(ret);
            return false;
        }

        int family;
        SocketUtils::GetAddrInfo(reinterpret_cast<sockaddr *>(&local_addr_),
                                 family,
                                 local_ip_,
                                 local_port_);
        return true;
    }

} // namespace sms